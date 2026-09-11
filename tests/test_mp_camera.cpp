// The port's camera as arithmetic: Camera::Clamp keeps a view inside a level,
// and Camera::Follow starts at camera_start, holds, then eases to the player.
//
// Pure: no level and no data. portals.json's camera numbers are _guess, so they
// are not pinned; this suite runs with numbers of its own and checks the shape of
// what they do. The view is level0's shape, 455 x 256 in a level 768 x 256.

#include "TestHarness.hpp"

#include "sim/Camera.hpp"

#include <cmath>
#include <string>

using namespace MagicPortals;

namespace {

constexpr double kDt = 1.0 / 60.0;
const glm::dvec2 kView(455.0, 256.0);
const glm::dvec2 kBounds(768.0, 256.0);

bool Near(const glm::dvec2& a, const glm::dvec2& b, double eps) {
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps;
}

std::string Px(const glm::dvec2& p) {
    return "(" + std::to_string(p.x) + ", " + std::to_string(p.y) + ")";
}

void TheViewStaysInsideTheLevel() {
    CHECK(Camera::Clamp({0.0, 0.0}, kView, kBounds) == glm::dvec2(227.5, 128.0));
    CHECK(Camera::Clamp({768.0, 256.0}, kView, kBounds) == glm::dvec2(540.5, 128.0));
    CHECK(Camera::Clamp({400.0, 50.0}, kView, kBounds) == glm::dvec2(400.0, 128.0));
    // A view wider than the level, as a wide window gives one, sits at its centre.
    CHECK(Camera::Clamp({0.0, 0.0}, {1000.0, 256.0}, kBounds) == glm::dvec2(384.0, 128.0));
    // And a taller level lets it travel up and down as well.
    CHECK(Camera::Clamp({100.0, 400.0}, kView, {512.0, 512.0}) == glm::dvec2(227.5, 384.0));
}

void ItHoldsThenEasesToThePlayer() {
    const Camera::Rules rules{0.12, 0.5};
    Camera::Follow follow;
    // level0: camera_start (651, 0) is held inside the level, at its right end.
    follow.Start(rules, {651.0, 0.0}, kView, kBounds);
    const glm::dvec2 start = follow.centrePx;
    CHECK(start == glm::dvec2(540.5, 128.0));

    const glm::dvec2 player(36.0, 86.0); // level0's spawn
    int tick = 0;
    for (; tick < 27; ++tick) follow.Tick(rules, player, kView, kBounds, kDt); // 0.45 s
    CHECK_MSG(follow.centrePx == start, "held for hold_time_s, now at " + Px(follow.centrePx));
    for (; tick < 36; ++tick) follow.Tick(rules, player, kView, kBounds, kDt); // 0.6 s
    CHECK_MSG(follow.centrePx.x < start.x, "moving by 0.6 s, at " + Px(follow.centrePx));

    // Each tick closes dt / follow_lag_s of the gap to where the view may go.
    const glm::dvec2 target = Camera::Clamp(player, kView, kBounds);
    const double before = follow.centrePx.x - target.x;
    follow.Tick(rules, player, kView, kBounds, kDt);
    const double after = follow.centrePx.x - target.x;
    CHECK_NEAR(static_cast<float>(after), static_cast<float>(before * (1.0 - kDt / rules.followLagS)));

    for (int i = 0; i < 180; ++i) follow.Tick(rules, player, kView, kBounds, kDt);
    CHECK_MSG(Near(follow.centrePx, glm::dvec2(227.5, 128.0), 0.5),
              "settled with the level's left edge at the view's, at " + Px(follow.centrePx));
}

void ItNeverShowsPastTheLevel() {
    const Camera::Rules rules{0.12, 0.0};
    Camera::Follow follow;
    follow.Start(rules, {384.0, 0.0}, kView, kBounds);
    const glm::dvec2 far[] = {{-500.0, -500.0}, {2000.0, 900.0}};
    for (const glm::dvec2& player : far) {
        bool inside = true;
        for (int i = 0; i < 240; ++i) {
            follow.Tick(rules, player, kView, kBounds, kDt);
            const glm::dvec2 c = follow.centrePx;
            inside = inside && c.x - kView.x / 2 >= 0.0 && c.x + kView.x / 2 <= kBounds.x &&
                     c.y - kView.y / 2 >= 0.0 && c.y + kView.y / 2 <= kBounds.y;
        }
        CHECK_MSG(inside, "following a player at " + Px(player));
    }
    // A window reshaped under it: the camera is held inside again on the next tick.
    follow.centrePx = glm::dvec2(700.0, 128.0);
    follow.Tick(rules, glm::dvec2(700.0, 128.0), kView, kBounds, kDt);
    CHECK(follow.centrePx.x <= 540.5);
}

void runTests() {
    TheViewStaysInsideTheLevel();
    ItHoldsThenEasesToThePlayer();
    ItNeverShowsPastTheLevel();
}

} // namespace

TEST_MAIN("test_mp_camera", 12)
