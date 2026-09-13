#include "sim/GravityWell.hpp"

#include "core/Components.hpp"
#include "core/DetMath.hpp"
#include "core/Json.hpp"
#include "sim/Units.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace MagicPortals::GravityWell {

namespace {

namespace Json = Supersonic::Json;

using Supersonic::RigidBodyComponent;
using Supersonic::TransformComponent;

// PI/2. INFERRED rather than decoded: PIb is a global the decoder renders by
// name and never defines, so every appearance of it is a use. Three independent
// ones fix it - smoothEnd is sin(v * PIb) and must reach 1 at v = 1, `PI + PIb`
// is a half-turn plus a quarter, and getAngle brackets its quadrants with
// PIb/2 and PI + PIb/2. gravitywell.json says so where the number is recorded.
constexpr double kPiOverTwo = 1.57079632679489661923;

// Interpolator.angelscript's smoothEnd: an ease-out that is 0 at 0 and 1 at 1,
// steep at the start and flat at the end. DetMath, so a well pulls the same on
// every C runtime - the contract Mover::Oscillation and Bounce::Bob keep.
double SmoothEnd(double v) {
    return static_cast<double>(Supersonic::DetMath::sin(static_cast<float>(v * kPiOverTwo)));
}

} // namespace

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot read";
        return false;
    }
    std::ostringstream text;
    text << file.rdbuf();

    // Named, not a temporary: the parser keeps a reference to what it reads.
    const std::string content = text.str();
    Json::Parser parser(content);
    Json::Value root;
    if (!parser.Parse(root)) {
        error = path + ": " + parser.Error();
        return false;
    }

    const auto number = [&](const char* group, const char* key, double& into) -> bool {
        const Json::Value& block = root[group];
        if (!block.Has(key) || !block[key].IsNumber()) {
            error = path + ": " + std::string(group) + "." + key + " is missing or not a number";
            return false;
        }
        into = block[key].AsNumber();
        return true;
    };

    Rules read;
    if (!number("force", "length_zero_gravity", read.forceZeroGravity)) return false;
    if (!number("force", "length_with_gravity", read.forceWithGravity)) return false;
    if (!number("force", "reference_frame_ms", read.referenceFrameMs)) return false;
    if (!number("zone", "shrink_px", read.zoneShrinkPx)) return false;

    // A well with no pull is a solid circle with a no-portal zone round it, and
    // five levels would look built and play wrong.
    if (read.forceZeroGravity <= 0.0 || read.forceWithGravity <= 0.0) {
        error = path + ": force.length_zero_gravity and force.length_with_gravity are above zero";
        return false;
    }
    // A reference frame of nothing divides by zero.
    if (read.referenceFrameMs <= 0.0) {
        error = path + ": force.reference_frame_ms is above zero";
        return false;
    }
    // The shrink takes SIZE off the antiportal, so it may not be negative, and a
    // shrink wider than the smallest well in the game (64 px radius, so 128 of
    // size) would give that well a zone of nothing at all.
    if (read.zoneShrinkPx < 0.0) {
        error = path + ": zone.shrink_px is not negative";
        return false;
    }

    out = std::move(read);
    return true;
}

glm::vec3 Pull(const Well& well, const Rules& rules, const glm::dvec2& bodyPx, bool noGravity, float dt) {
    const glm::dvec2 toCentre = well.atPx - bodyPx;
    const double squaredDist = toCentre.x * toCentre.x + toCentre.y * toCentre.y;
    const double squaredRadius = well.radiusPx * well.radiusPx;

    // A strict less-than, as the original's CMPf/TS is: a body exactly on the
    // rim is outside.
    if (!(squaredDist < squaredRadius)) return glm::vec3(0.0f);
    const double length = std::sqrt(squaredDist);
    // Dead centre, which normalize cannot answer. Nothing, rather than a NaN
    // that would poison the body's velocity for the rest of the level.
    if (length <= 0.0) return glm::vec3(0.0f);

    const glm::dvec2 direction = toCentre / length;
    const double bias = SmoothEnd(1.0 - squaredDist / squaredRadius);
    const double strength = noGravity ? rules.forceZeroGravity : rules.forceWithGravity;
    const double fpsFix = (static_cast<double>(dt) * 1000.0) / rules.referenceFrameMs;
    const double force = bias * strength * fpsFix;

    // Turned over in y: the remake's pixels count down, the engine's metres up.
    // The magnitude is already metres per second - gravitywell.json says why it
    // needs no conversion, as the portal recoil does not.
    return glm::vec3(static_cast<float>(direction.x * force), static_cast<float>(-direction.y * force), 0.0f);
}

void State::Tick(entt::registry& registry, float dt) {
    if (wells.empty() || dt <= 0.0f) return;

    // Every DYNAMIC body: the port's reading of DynamicBodyChooser, which
    // rejects IsStatic() and anything without a physics controller. Here that is
    // a RigidBodyComponent that is not kinematic - a kinematic body is moved by
    // code each tick and a well would only fight whatever moves it.
    for (auto [entity, transform, rigid] : registry.view<TransformComponent, RigidBodyComponent>().each()) {
        if (rigid.isKinematic) continue;
        const glm::dvec2 bodyPx(transform.position.x * Units::kPixelsPerMetre,
                                -transform.position.y * Units::kPixelsPerMetre);
        for (const Well& well : wells) {
            rigid.velocity += Pull(well, rules, bodyPx, noGravity, dt);
        }
    }
}

const Well* State::Find(const std::string& name) const {
    for (const Well& well : wells) {
        if (well.name == name) return &well;
    }
    return nullptr;
}

bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const Rules& rules, State& out, std::string& error) {
    out = State{};
    out.rules = rules;

    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;
        if (Roles::RoleOf(roles, node) != Roles::kGravityWell) continue;

        const Tscn::Value* position = node.Find("position");
        if (position == nullptr || position->kind != Tscn::Value::Kind::Vector2) {
            error = node.name + " is a gravity well with no position";
            return false;
        }
        const Tscn::Value* radius = node.Meta("radius");
        double radiusPx = 0.0;
        if (radius == nullptr || !radius->AsNumber(radiusPx) || radiusPx <= 0.0) {
            error = node.name + " is a gravity well with no radius";
            return false;
        }

        Well well;
        well.name = node.name;
        well.atPx = glm::dvec2(position->numbers[0], position->numbers[1]);
        well.radiusPx = radiusPx;
        out.wells.push_back(well);
    }
    return true;
}

} // namespace MagicPortals::GravityWell
