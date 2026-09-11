#pragma once

// The camera the port plays with: the remake's follow (main.gd:123-137), and
// two things the original has that the remake leaves out.
//
// - It starts at the level's camera_start and eases toward the player, closing
//   dt / follow_lag_s of the gap each tick. Before it moves it holds for
//   hold_time_s: the original has cameraHoldTime and cameraHoldElapsedTime, and
//   camera_start sits near a level's exit, so the hold shows the goal before the
//   camera goes to the player. Both numbers are portals.json's camera block,
//   and both are _guess. The remake reads neither the hold nor the limits.
// - It eases per tick, where the remake eases per frame, so a tap resolved on
//   the tick meets the camera a replay meets. InterpolatedCameraComponent draws
//   it between ticks.
// - The view never shows past (0, 0) or the level_bounds marker. That is
//   clampCameraPos in the original, and the marker's own role note. The remake's
//   camera has no limits, so this is the port's, from the data.
//
// camera_start is taken as the view's centre, as the remake takes it. Ethanon
// places its own camera by the top-left corner, but read either way the view is
// nearly the same once held inside the level: every camera_start sits at or near
// y 0, and near a level's far end.

#include <string>

#include <glm/glm.hpp>

namespace MagicPortals::Camera {

struct Rules {
    double followLagS = 0.0; // camera.follow_lag_s, _guess
    double holdTimeS = 0.0;  // camera.hold_time_s, _guess
};

// From portals.json's camera block, strictly.
bool LoadRules(const std::string& path, Rules& out, std::string& error);

// How much of a level the view shows, as its height in the level's pixels: the
// port's own view.json, whose note gives the evidence. The width follows the
// window's shape.
bool LoadViewHeight(const std::string& path, double& heightPx, std::string& error);

// The centre a view of `viewPx` may take in a level running from (0, 0) to
// `boundsPx`: as near `wantPx` as keeps the whole view inside, or the level's
// centre on an axis the view is wider than.
glm::dvec2 Clamp(const glm::dvec2& wantPx, const glm::dvec2& viewPx, const glm::dvec2& boundsPx);

struct Follow {
    glm::dvec2 centrePx{0.0};
    double holdLeftS = 0.0;

    void Start(const Rules& rules, const glm::dvec2& cameraStartPx, const glm::dvec2& viewPx,
               const glm::dvec2& boundsPx);

    // One tick. The view is passed each time because a window can change shape
    // under it, and the camera is held inside the level again when it does.
    void Tick(const Rules& rules, const glm::dvec2& playerPx, const glm::dvec2& viewPx, const glm::dvec2& boundsPx,
              double dt);
};

} // namespace MagicPortals::Camera
