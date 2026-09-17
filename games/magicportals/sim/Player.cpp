#include "sim/Player.hpp"

#include "core/Components.hpp"
#include "core/Json.hpp"
#include "core/PhysicsSystem.hpp"
#include "sim/LevelBuilder.hpp"
#include "sim/Shot.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace MagicPortals::Player {

using Supersonic::CapsuleColliderComponent;
using Supersonic::RigidBodyComponent;
using Supersonic::TagComponent;
using Supersonic::TransformComponent;

namespace {

// Godot's move_toward: a step of at most `delta` towards `to`, never past it.
float MoveToward(float from, float to, float delta) {
    if (std::fabs(to - from) <= delta) return to;
    return from + (to > from ? delta : -delta);
}

} // namespace

bool LoadTuning(const std::string& path, Tuning& out, std::string& error) {
    namespace Json = Supersonic::Json;
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

    const struct {
        const char* section;
        const char* key;
        double Tuning::*field;
    } fields[] = {
        {"movement", "walk_speed_px_s", &Tuning::walkSpeedPx},
        {"movement", "acceleration_px_s2", &Tuning::accelerationPx},
        {"movement", "friction_px_s2", &Tuning::frictionPx},
        {"movement", "air_control", &Tuning::airControl},
        {"gravity", "gravity_px_s2", &Tuning::gravityPx},
        {"gravity", "max_fall_speed_px_s", &Tuning::maxFallPx},
        {"body", "width_px", &Tuning::widthPx},
        {"body", "height_px", &Tuning::heightPx},
    };
    out = Tuning{};
    for (const auto& field : fields) {
        const Json::Value& value = root[field.section][field.key];
        if (!value.IsNumber()) {
            error = path + ": " + field.section + "." + field.key + " is not a number";
            return false;
        }
        out.*(field.field) = value.AsNumber();
    }
    return true;
}

entt::entity Spawn(entt::registry& registry, const glm::dvec2& atPx, const Tuning& tuning) {
    const entt::entity entity = registry.create();
    registry.emplace<TransformComponent>(entity).position = Units::ToWorld(atPx.x, atPx.y);
    auto& capsule = registry.emplace<CapsuleColliderComponent>(entity);
    capsule.radius = Units::ToMetres(tuning.widthPx * 0.5);
    capsule.height = Units::ToMetres(tuning.heightPx);
    auto& rigid = registry.emplace<RigidBodyComponent>(entity);
    rigid.freezeRotation = true;
    rigid.lockPosition = LevelBuilder::kPlaneLockPosition;
    rigid.friction = kFriction;
    rigid.restitution = LevelBuilder::kBodyRestitution;
    rigid.useGravity = false; // its own, applied in Steer
    rigid.allowSleep = false; // steered every tick
    registry.emplace<TagComponent>(entity, TagComponent{"player"});
    return entity;
}

bool Grounded(entt::registry& registry, entt::entity player, const Tuning& tuning) {
    const glm::vec3 centre = registry.get<TransformComponent>(player).position;
    const glm::vec3 feet = centre - glm::vec3(0.0f, Units::ToMetres(tuning.heightPx * 0.5 - 1.0), 0.0f);
    return Supersonic::PhysicsSystem::IsGrounded(registry, feet, Units::ToMetres(4.0), player);
}

glm::dvec2 PushAim(entt::registry& registry, entt::entity player, const Tuning& tuning, float direction,
                   double airShare) {
    const auto* rigid = registry.try_get<RigidBodyComponent>(player);
    const glm::dvec2 velocityPx = rigid != nullptr ? Units::ToPixels(rigid->velocity) : glm::dvec2(0.0);
    // applyForces instructions 38-56: the walk across the vertical velocity;
    // 63-84: where isTouchingGround is not set, the body's own x instead.
    if (Grounded(registry, player, tuning)) return glm::dvec2(direction * tuning.walkSpeedPx, velocityPx.y);
    return glm::dvec2(velocityPx.x * airShare, velocityPx.y);
}

bool Pushing(entt::registry& registry, entt::entity player, const glm::dvec2& aimPx, double reachPx,
             const glm::dvec2& offsetPx) {
    // detectPushing instructions 3-6: no x, no push, whatever lies ahead.
    if (aimPx.x == 0.0) return false;
    const glm::dvec2 fromPx = Units::ToPixels(registry.get<TransformComponent>(player).position);
    const glm::dvec2 toPx = fromPx + aimPx / std::sqrt(aimPx.x * aimPx.x + aimPx.y * aimPx.y) * reachPx + offsetPx;
    return Shot::ClosestContact(registry, fromPx, toPx, player).has_value();
}

void Steer(entt::registry& registry, entt::entity player, const Tuning& tuning, float direction, float dt) {
    const bool grounded = Grounded(registry, player, tuning);
    auto& rigid = registry.get<RigidBodyComponent>(player);

    rigid.velocity.y =
        std::max(rigid.velocity.y - Units::ToMetres(tuning.gravityPx) * dt, -Units::ToMetres(tuning.maxFallPx));

    // player.gd:101-105, in metres.
    const float target = direction * Units::ToMetres(tuning.walkSpeedPx);
    float rate = Units::ToMetres(direction != 0.0f ? tuning.accelerationPx : tuning.frictionPx);
    if (!grounded) rate *= static_cast<float>(tuning.airControl);
    rigid.velocity.x = MoveToward(rigid.velocity.x, target, rate * dt);
}

} // namespace MagicPortals::Player
