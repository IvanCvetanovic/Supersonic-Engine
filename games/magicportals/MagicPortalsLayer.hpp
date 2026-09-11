#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "core/Components.hpp"
#include "core/EngineLayer.hpp"

#include "sim/Game.hpp"

namespace MagicPortals {

// Magic Portals' first view: level30 as the port plays it, drawn as boxes.
//
// Deliberately plain, like HUSK's first view. It draws a box for each body in
// the colour of what it is, the placed portals, the crystals still out, the
// buttons and the exit, and a few lines of text. What it has to get right is
// the seam:
//
//  - The level is the layer's (Game::Level), and it runs on the port's 60 Hz
//    tick, set on the engine's clock. The app steps physics before each
//    OnFixedUpdate, so the layer runs Game::AfterStep then Game::BeforeStep.
//    That is the suites' tick, with the app's step between the halves.
//  - Input is read on the tick. Left and right held are the remake's two-button
//    pad, and a tap places a portal at the point in the level under it. There
//    is one tap per tick at most, so a frame cannot spend the budget twice in
//    one step.
//  - The camera is orthographic and fixed, fitted to the level's bounds. level30
//    fits on one screen, so there is no follow, pan or zoom; the port's planning
//    doc lists those as out of scope.
class MagicPortalsLayer final : public Supersonic::EngineLayer {
public:
    // The port's tick: the remake's physics runs at Godot's default 60 Hz.
    static constexpr float kTick = 1.0f / 60.0f;

    // The actions this layer binds at attach, named so tests and a recording
    // can drive them without knowing which buttons they sit on.
    static constexpr const char* kLeft = "mp.left";          // Left arrow
    static constexpr const char* kLeftAlt = "mp.left.alt";   // A
    static constexpr const char* kRight = "mp.right";        // Right arrow
    static constexpr const char* kRightAlt = "mp.right.alt"; // D
    static constexpr const char* kTap = "mp.tap";            // left mouse: place a portal

    MagicPortalsLayer(std::string levelPath, std::string dataDirectory, std::filesystem::path prismDirectory);

    const char* Name() const override { return "Magic Portals"; }

    void OnAttach(entt::registry& registry) override;
    void OnDetach(entt::registry& registry) override;
    void OnFixedUpdate(entt::registry& registry, float fixedDelta) override;
    void OnUpdate(entt::registry& registry, float deltaTime) override;

    // Why the level did not load; empty when it did. A level that fails leaves
    // the layer showing that, rather than throwing out of OnAttach.
    const std::string& LoadError() const { return m_loadError; }

    // The level being played, or null when it did not load.
    const Game::Level* SimLevel() const { return m_loaded ? &m_level : nullptr; }

    // A screen point (Input's coordinates) as the point in the level under it,
    // in the remake's pixels. False when there is no viewport or camera.
    bool ScreenToLevelPx(const entt::registry& registry, const glm::vec2& screenPoint, glm::dvec2& outPx) const;

private:
    // A body the level built, and the box standing for it. The box sits at the
    // body's shape, offset from its entity in the body's own frame.
    struct Drawn {
        entt::entity body{entt::null};
        entt::entity box{entt::null};
        glm::dvec2 offsetPx{0.0};
        glm::dvec2 sizePx{0.0};
        float depth{0.4f};
    };

    // The camera's shape for a viewport: its aspect, and an orthoHeight that fits
    // the whole level with a margin.
    void fitCamera(Supersonic::CameraComponent& camera, const glm::vec2& viewportSize) const;

    void bindInput();
    void buildCamera(entt::registry& registry);
    void buildDrawables(entt::registry& registry);
    void buildHud(entt::registry& registry);
    float readInput(entt::registry& registry);
    void syncDrawables(entt::registry& registry);
    void updateHud(entt::registry& registry);

    entt::entity makeBox(entt::registry& registry, const char* tag, const glm::vec3& centre, const glm::vec3& size,
                         const glm::vec3& colour);
    void placeBox(entt::registry& registry, entt::entity box, const glm::dvec2& centrePx, const glm::dvec2& sizePx,
                  float z, float depth, float rotation) const;

    std::string m_levelPath;
    std::string m_dataDirectory;
    std::filesystem::path m_prismDirectory;

    Game::Data m_data;
    Game::Level m_level;
    bool m_loaded{false};
    std::string m_loadError;
    glm::dvec2 m_boundsPx{0.0}; // the level_bounds marker: the level runs from (0, 0) to here

    entt::entity m_camera{entt::null};
    entt::entity m_light{entt::null};
    std::vector<Drawn> m_bodies;
    entt::entity m_player{entt::null};
    std::vector<entt::entity> m_buttons;  // one per Puzzle button, in its order
    std::vector<entt::entity> m_crystals; // one per crystal, null once collected
    entt::entity m_exit{entt::null};
    std::vector<entt::entity> m_portals;  // one per placed portal

    struct Hud {
        entt::entity status{entt::null};
        entt::entity controls{entt::null};
    };
    Hud m_hud;
};

} // namespace MagicPortals
