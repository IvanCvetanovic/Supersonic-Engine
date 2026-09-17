#include "sim/Shot.hpp"

#include "core/Components.hpp"
#include "core/DetMath.hpp"
#include "core/Json.hpp"
#include "sim/LevelBuilder.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>

namespace MagicPortals::Shot {

using glm::dvec2;
using Supersonic::BoxColliderComponent;
using Supersonic::ConvexHullColliderComponent;
using Supersonic::SphereColliderComponent;
using Supersonic::TransformComponent;

namespace {

// The remake's pixels as the plane's metres, +y up, in double.
dvec2 Metres(const dvec2& px) { return dvec2(px.x, -px.y) / Units::kPixelsPerMetre; }

double Cross(const dvec2& a, const dvec2& b) { return a.x * b.y - a.y * b.x; }

// An entity's frame in the plane: where it is, and its turn about z. DetMath
// rather than the platform's sin and cos, so a shot grazing a turned body is
// judged the same on every C runtime.
struct Frame {
    dvec2 origin{0.0};
    double c = 1.0;
    double s = 0.0;

    explicit Frame(const TransformComponent& transform)
        : origin(transform.position.x, transform.position.y),
          c(Supersonic::DetMath::cos(transform.rotation.z)),
          s(Supersonic::DetMath::sin(transform.rotation.z)) {}

    dvec2 operator()(const dvec2& local) const {
        return origin + dvec2(c * local.x - s * local.y, s * local.x + c * local.y);
    }
};

// Where the segment a-b first enters a convex polygon, as a fraction of the way
// (Cyrus-Beck). 0 when a is inside it.
std::optional<double> IntoPolygon(const dvec2& a, const dvec2& b, const std::vector<dvec2>& polygon) {
    const std::size_t n = polygon.size();
    if (n < 3) return std::nullopt;
    double area = 0.0;
    for (std::size_t i = 0; i < n; ++i) area += Cross(polygon[i], polygon[(i + 1) % n]);
    const double outward = area >= 0.0 ? 1.0 : -1.0;
    const dvec2 d = b - a;
    double enter = 0.0;
    double leave = 1.0;
    for (std::size_t i = 0; i < n; ++i) {
        const dvec2 p = polygon[i];
        const dvec2 edge = polygon[(i + 1) % n] - p;
        const dvec2 normal = outward * dvec2(edge.y, -edge.x);
        // Inside this edge's half-plane is dot(normal, a + t d - p) <= 0.
        const double num = glm::dot(normal, p - a);
        const double den = glm::dot(normal, d);
        if (den == 0.0) {
            if (num < 0.0) return std::nullopt;
            continue;
        }
        const double t = num / den;
        if (den < 0.0) {
            enter = std::max(enter, t);
        } else {
            leave = std::min(leave, t);
        }
        if (enter > leave) return std::nullopt;
    }
    return enter;
}

std::optional<double> IntoCircle(const dvec2& a, const dvec2& b, const dvec2& centre, double radius) {
    const dvec2 d = b - a;
    const dvec2 f = a - centre;
    const double c = glm::dot(f, f) - radius * radius;
    if (c <= 0.0) return 0.0;
    const double aa = glm::dot(d, d);
    if (aa == 0.0) return std::nullopt;
    const double bb = 2.0 * glm::dot(f, d);
    const double disc = bb * bb - 4.0 * aa * c;
    if (disc < 0.0) return std::nullopt;
    const double t = (-bb - std::sqrt(disc)) / (2.0 * aa);
    if (t < 0.0 || t > 1.0) return std::nullopt;
    return t;
}

} // namespace

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    namespace Json = Supersonic::Json;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot open";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    Json::Parser parser(text);
    Json::Value root;
    if (!parser.Parse(root)) {
        error = path + ": " + parser.Error();
        return false;
    }
    if (!root.IsObject() || !root.Has("flight") || !root["flight"].IsObject() || !root["flight"].Has("speed_px_s") ||
        !root["flight"]["speed_px_s"].IsNumber()) {
        error = path + ": flight.speed_px_s is missing or not a number";
        return false;
    }
    Rules read;
    read.speedPx = root["flight"]["speed_px_s"].AsNumber();
    if (read.speedPx <= 0.0) {
        error = path + ": flight.speed_px_s is not above 0";
        return false;
    }
    const Json::Value& reflection = root["reflection"];
    if (!reflection.IsObject() || !reflection.Has("catch_radius_px") || !reflection["catch_radius_px"].IsNumber() ||
        reflection["catch_radius_px"].AsNumber() <= 0.0 || !reflection.Has("max_reflections") ||
        !reflection["max_reflections"].IsNumber() || reflection["max_reflections"].AsNumber() < 0.0 ||
        reflection["max_reflections"].AsNumber() != std::floor(reflection["max_reflections"].AsNumber())) {
        error = path + ": reflection needs catch_radius_px above 0 and a whole max_reflections from 0";
        return false;
    }
    read.reflectRadiusPx = reflection["catch_radius_px"].AsNumber();
    read.maxReflections = static_cast<int>(reflection["max_reflections"].AsNumber());
    const Json::Value& plane = reflection["default_plane"];
    if (!plane.IsString() || (plane.AsString("") != "vertical" && plane.AsString("") != "horizontal")) {
        error = path + ": reflection.default_plane is vertical or horizontal";
        return false;
    }
    read.defaultPlaneVertical = plane.AsString("") == "vertical";
    out = read;
    return true;
}

namespace {

// What the segment from `fromPx` to `toPx` meets first, other than `ignore`.
// `sensors` lets a trigger be met; `fromInside` lets a body the segment starts
// inside (or on) be met at once, at 0.
std::optional<Hit> Meets(entt::registry& registry, const glm::dvec2& fromPx, const glm::dvec2& toPx, entt::entity ignore,
                         bool sensors, bool fromInside) {
    const dvec2 a = Metres(fromPx);
    const dvec2 b = Metres(toPx);
    std::optional<Hit> first;
    const auto consider = [&first, fromInside](entt::entity body, std::optional<double> along) {
        if (!along || (!fromInside && *along <= 0.0)) return;
        if (!first || *along < first->along) first = Hit{body, *along};
    };

    for (auto [entity, transform, box] : registry.view<TransformComponent, BoxColliderComponent>().each()) {
        if (entity == ignore || (box.isTrigger && !sensors)) continue;
        const Frame frame(transform);
        const dvec2 centre(box.center.x, box.center.y);
        const dvec2 half(box.size.x * 0.5, box.size.y * 0.5);
        const std::vector<dvec2> corners{frame(centre + dvec2(-half.x, -half.y)), frame(centre + dvec2(half.x, -half.y)),
                                         frame(centre + dvec2(half.x, half.y)), frame(centre + dvec2(-half.x, half.y))};
        consider(entity, IntoPolygon(a, b, corners));
    }
    for (auto [entity, transform, sphere] : registry.view<TransformComponent, SphereColliderComponent>().each()) {
        if (entity == ignore || (sphere.isTrigger && !sensors)) continue;
        const Frame frame(transform);
        consider(entity, IntoCircle(a, b, frame(dvec2(sphere.center.x, sphere.center.y)), sphere.radius));
    }
    for (auto [entity, transform, hull, outline] :
         registry.view<TransformComponent, ConvexHullColliderComponent, LevelBuilder::PlanePolygon>().each()) {
        if (entity == ignore || (hull.isTrigger && !sensors)) continue;
        const Frame frame(transform);
        std::vector<dvec2> points;
        points.reserve(outline.points.size());
        for (const glm::vec2& point : outline.points) points.push_back(frame(dvec2(point)));
        consider(entity, IntoPolygon(a, b, points));
    }
    return first;
}

} // namespace

std::optional<Hit> FirstBody(entt::registry& registry, const glm::dvec2& fromPx, const glm::dvec2& toPx,
                             entt::entity ignore) {
    return Meets(registry, fromPx, toPx, ignore, false, true);
}

std::optional<Hit> ClosestContact(entt::registry& registry, const glm::dvec2& fromPx, const glm::dvec2& toPx,
                                  entt::entity ignore) {
    return Meets(registry, fromPx, toPx, ignore, true, false);
}

std::optional<double> Enters(const glm::dvec2& fromPx, const glm::dvec2& toPx, const Trigger::Box& box) {
    const dvec2 lo = dvec2(box.centre) - dvec2(box.half);
    const dvec2 hi = dvec2(box.centre) + dvec2(box.half);
    return IntoPolygon(Metres(fromPx), Metres(toPx), {lo, dvec2(hi.x, lo.y), hi, dvec2(lo.x, hi.y)});
}

std::optional<double> EntersCircle(const glm::dvec2& fromPx, const glm::dvec2& toPx, const glm::dvec2& centrePx,
                                   double radiusPx) {
    const glm::dvec2 d = toPx - fromPx;
    const glm::dvec2 f = fromPx - centrePx;
    const double c = glm::dot(f, f) - radiusPx * radiusPx;
    if (c <= 0.0) return 0.0;
    const double a = glm::dot(d, d);
    if (a <= 0.0) return std::nullopt;
    const double b = 2.0 * glm::dot(f, d);
    const double discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0) return std::nullopt;
    const double along = (-b - std::sqrt(discriminant)) / (2.0 * a);
    if (along < 0.0 || along > 1.0) return std::nullopt;
    return along;
}

} // namespace MagicPortals::Shot
