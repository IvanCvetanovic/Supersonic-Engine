#pragma once

#include <cstdint>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "core/EngineLayer.hpp"

namespace WolfBrigade {

// Phase 0 of the port plan: get the lane on screen and measure it.
//
// See docs/planning/2026-08-25-wolf-brigade-port.md. This is a SPIKE, not the
// port. It renders a hardcoded lane of coloured quads through the engine's
// existing perspective renderer with emissive materials, because that is near
// enough to Godot's ColorRect to prove the seam without first building an
// orthographic camera or an unlit path.
//
// It exists to answer three questions that every estimate in that plan depends
// on, and which cannot be answered by reading code:
//
//   1. Can a game live outside the engine at all? Nothing had ever called
//      SupersonicApp::PushLayer before this - the seam was argued for and
//      never used, which is not the same as working.
//   2. What does the frame cost at Wolf Brigade's real drawable count? The plan
//      says 350-400 for a busy campaign frame and unbounded for endless, and
//      the renderer submits one draw and one push constant per entity with no
//      instancing and no sort.
//   3. Does the lane read at all - is a side-on 1D strip legible through a
//      perspective camera pointed at it?
//
// Everything here is deliberately disposable. When Phase 1 lands an
// orthographic camera and a flat-colour path, the quad construction below is
// what gets deleted first.
class WolfBrigadeLayer final : public Supersonic::EngineLayer {
public:
    // How many units to spawn along the lane.
    //
    // The plan's campaign figure is 40-60 units at 5 drawables each; `units`
    // here is DRAWABLES, so the default is one busy campaign frame. Raise it to
    // measure the endless case, which has no data-driven ceiling at all.
    // `sunIntensity` exists so the unlit path can be PROVEN rather than
    // admired: render the same lane twice with different light and the unlit
    // quads must not move by one bit, while the lit ground must.
    explicit WolfBrigadeLayer(int drawables = 400, float sunIntensity = 1.4f)
        : m_requested(drawables), m_sunIntensity(sunIntensity) {}

    const char* Name() const override { return "WolfBrigade (Phase 0 spike)"; }

    void OnAttach(entt::registry& registry) override;
    void OnUpdate(entt::registry& registry, float deltaTime) override;

private:
    // One Wolf Brigade unit, as the engine sees it.
    //
    // Five entities, matching scenes/unit.tscn exactly - SelectionRing, Body,
    // HPBar/Bg, HPBar/Fill, Label - because the count is the thing being
    // measured. Four quads and a label; the label is a quad here too, since
    // world-space text is Phase 2 and its COST is what this needs, not its
    // appearance.
    struct Unit {
        entt::entity ring{entt::null};
        entt::entity body{entt::null};
        entt::entity barBg{entt::null};
        entt::entity barFill{entt::null};
        entt::entity label{entt::null};

        float laneX{0.0f};
        float speed{0.0f};
        float health{1.0f};
    };

    // `layer` is the draw order, exactly as unit.tscn's child order is: higher
    // is drawn on top. Every quad sits on the SAME plane, so the key is the
    // only thing separating them.
    entt::entity makeQuad(entt::registry& registry, const char* tag, const glm::vec3& position,
                          const glm::vec3& size, const glm::vec3& colour, int32_t layer,
                          bool unlit = true);

    std::vector<Unit> m_units;
    int m_requested{0};
    float m_sunIntensity{1.4f};
    float m_elapsed{0.0f};
};

} // namespace WolfBrigade
