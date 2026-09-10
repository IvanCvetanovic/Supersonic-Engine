#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "core/EngineLayer.hpp"

#include "sim/World.hpp"

namespace husk {

// HUSK's first view: the ported simulation, drawn as boxes.
//
// Deliberately ugly (the 19 August two-games record): a box per unit, building,
// source and item in the colour and size the catalogs give it, raised terrain
// as boxes, obstacles as dark blocks, and four lines of text. What it has to get
// RIGHT is the seam, and the seam is small:
//
//  - The World is the layer's, and the engine knows nothing of it. OnFixedUpdate
//    steps it once per engine tick, and the tick is HUSK's 20 Hz, set on the
//    engine's SimulationClock at attach. A step is the ported `step()` and
//    nothing else, so what runs here is the thing the suites hold to the Rust
//    game's hashes.
//
//  - The picture follows the sim, never the other way. Drawables are written
//    from sim positions after each step and interpolated by the engine between
//    ticks. Nothing the view does reaches the World except an OrderMsg on its
//    queue - which is exactly how the Rust client talks to its sim.
//
//  - Input is read on the TICK, and that includes the camera. Clicks arrive
//    through bound actions and Input's per-tick pointer; the camera pans on
//    bound keys, the same pointer and Input::TickScroll, and the engine draws
//    it between ticks through InterpolatedCameraComponent. So a click is always
//    resolved against the camera its tick had, and a recorded session replays
//    its orders exactly - which a camera moved per frame could not promise.
class HuskLayer final : public Supersonic::EngineLayer {
public:
    static constexpr const char* kDefaultMission = "first_light";
    // Not a mission: the M2 macro sandbox (scenario.rs), a base and a few units
    // on the M0 map, for looking at the economy without a script driving it.
    static constexpr const char* kSandbox = "sandbox";

    // The actions this layer binds at attach. Named so tests and a recording
    // can drive them without knowing which buttons they sit on.
    static constexpr const char* kSelect = "husk.select";   // left mouse
    static constexpr const char* kCommand = "husk.command"; // right mouse
    static constexpr const char* kQueue = "husk.queue";     // shift: queue the order
    static constexpr const char* kStop = "husk.stop";       // S
    static constexpr const char* kHold = "husk.hold";       // H
    // camera.rs pans on the arrows (WASD is the order hotkeys') and drags on
    // the middle button.
    static constexpr const char* kPanLeft = "husk.pan.left";
    static constexpr const char* kPanRight = "husk.pan.right";
    static constexpr const char* kPanUp = "husk.pan.up";
    static constexpr const char* kPanDown = "husk.pan.down";
    static constexpr const char* kDrag = "husk.drag";

    explicit HuskLayer(std::string mission = kDefaultMission, uint64_t seed = kDefaultSeed);

    const char* Name() const override { return "HUSK"; }

    void OnAttach(entt::registry& registry) override;
    void OnDetach(entt::registry& registry) override;
    void OnFixedUpdate(entt::registry& registry, float fixedDelta) override;
    void OnUpdate(entt::registry& registry, float deltaTime) override;

    // The simulation this layer runs; null before OnAttach and after OnDetach.
    // Mutable for the tests, which arrange the world and then read it back.
    const World* SimWorld() const { return m_world.get(); }
    World* SimWorld() { return m_world.get(); }

    // Why the mission did not load, empty when it did. A mission that fails
    // leaves the layer running an empty world rather than throwing out of
    // OnAttach, and the HUD says so.
    const std::string& LoadError() const { return m_loadError; }

    // The player's selection, as SimIds, in the order they were picked.
    const std::vector<uint32_t>& Selection() const { return m_selection; }

    // The drawable standing for a sim entity, or null when it has none.
    entt::entity DrawableFor(uint32_t simId) const;

    // A screen point (Input's coordinates) as the point on the terrain under
    // it. False when there is no viewport or camera yet, or the ray never
    // meets the ground.
    bool ScreenToGround(const entt::registry& registry, const glm::vec2& screenPoint,
                        Vec2& outGround) const;

    // The rig from camera.rs: a focus on the ground and a 0..1 zoom.
    glm::vec2 CameraFocus() const { return m_focus; }
    float CameraZoom() const { return m_zoom; }

private:
    // The camera's matrices for the current viewport, so a batch of
    // projections builds them once.
    struct ScreenMap {
        glm::mat4 viewProj{1.0f};
        glm::vec2 origin{0.0f};
        glm::vec2 size{0.0f};

        bool project(const glm::vec3& world, glm::vec2& outScreen) const;
    };
    std::optional<ScreenMap> screenMap(const entt::registry& registry) const;

    // select.rs's pick_distance_px: the pointer's distance to an entity's
    // whole vertical span, less its projected footprint.
    static float pickDistancePx(const ScreenMap& map, const glm::vec2& cursor, const glm::vec3& base,
                                float height, float radius);

    // The box an entity is drawn as.
    struct Box {
        glm::vec3 size{0.5f};
        glm::vec3 colour{1.0f};
    };
    Box boxFor(const Entity& e, bool selected) const;
    glm::vec3 baseOf(const Entity& e) const;

    // The selection split the way input.rs splits it: team-0 units only,
    // workers apart from fighters, and the selected heroes.
    struct Split {
        std::vector<uint32_t> workers;
        std::vector<uint32_t> fighters;
        std::vector<uint32_t> heroes;
        std::vector<uint32_t> channelers; // workers and heroes: both can channel
        std::vector<uint32_t> all;        // workers, then fighters
    };
    Split splitSelection() const;

    void bindInput();
    void buildTerrain(entt::registry& registry);
    void buildCamera(entt::registry& registry);
    void buildHud(entt::registry& registry);

    void syncDrawables(entt::registry& registry);
    void applyInput(entt::registry& registry);
    void resolveSelection(entt::registry& registry, const glm::vec2& start, const glm::vec2& end,
                          bool additive);
    void issueCommand(entt::registry& registry, const glm::vec2& pointer, bool queued);
    void readMissionEvents();
    void drainEvents();
    void updateHud(entt::registry& registry);

    // camera.rs's input, on the tick: pan, edge, drag and zoom, then the rig.
    void moveCamera(entt::registry& registry);

    // The rig into the CameraComponent: distance and pitch from the zoom,
    // looking at the focus, yaw locked north.
    void placeCamera(entt::registry& registry);

    // Terrain height under a sim position, from the nav grid's tier.
    float groundHeight(Vec2 p) const;

    entt::entity makeBox(entt::registry& registry, const char* tag, const glm::vec3& centre,
                         const glm::vec3& size, const glm::vec3& colour);

    std::string m_mission;
    uint64_t m_seed;
    std::unique_ptr<World> m_world;
    std::string m_loadError;

    // One drawable per sim entity, keyed by SimId. Created when an entity
    // first appears and destroyed when it goes: sim spawns and deaths are a
    // few per second, and a pooled drawable handed to a new entity would
    // interpolate in from wherever the dead one was.
    std::map<uint32_t, entt::entity> m_drawables;

    // Terrain, the ground plane and the light. Built once: a mission's map
    // does not change during the mission.
    std::vector<entt::entity> m_static;

    entt::entity m_camera{entt::null};
    glm::vec2 m_focus{-32.0f, -32.0f}; // camera.rs's default, over the M0 spawn
    float m_zoom{0.45f};
    // The pivot height, smoothed so a cliff edge does not kick the camera.
    float m_focusHeight{0.0f};
    // A middle-drag in progress, and where the pointer was on the last tick.
    bool m_dragging{false};
    glm::vec2 m_dragLast{0.0f};

    std::vector<uint32_t> m_selection;
    bool m_selectHeld{false};
    glm::vec2 m_pressAt{0.0f};

    struct Hud {
        entt::entity resources{entt::null};
        entt::entity selection{entt::null};
        entt::entity mission{entt::null};
        entt::entity status{entt::null};
    };
    Hud m_hud;

    // The last few things the mission said, newest last.
    std::deque<std::string> m_messages;
};

} // namespace husk
