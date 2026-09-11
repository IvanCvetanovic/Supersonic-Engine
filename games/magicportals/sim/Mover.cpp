#include "sim/Mover.hpp"

#include "core/Components.hpp"
#include "sim/Units.hpp"

#include <algorithm>

namespace MagicPortals::Mover {

void MoveKinematic(entt::registry& registry, entt::entity entity, const glm::vec3& target, float dt) {
    auto& transform = registry.get<Supersonic::TransformComponent>(entity);
    auto& rigid = registry.get<Supersonic::RigidBodyComponent>(entity);
    rigid.velocity = dt > 0.0f ? (target - transform.position) / dt : glm::vec3(0.0f);
    transform.position = target;
}

void Door::Tick(entt::registry& registry, entt::entity entity, float dt) {
    const float step = dt / durationS;
    progress = std::clamp(progress + (opening ? step : -step), 0.0f, 1.0f);
    MoveKinematic(registry, entity, glm::mix(closed, open, progress), dt);
}

bool DoorFromNode(const Tscn::Node& node, Door& out, std::string& error) {
    const Tscn::Value* stride = node.Meta("stride");
    double milliseconds = 0.0;
    if (stride == nullptr || !stride->AsNumber(milliseconds)) {
        error = node.name + " has no stride";
        return false;
    }
    const Tscn::Value* position = node.Find("position");
    glm::dvec2 at(0.0);
    if (position != nullptr && position->kind == Tscn::Value::Kind::Vector2) {
        at = glm::dvec2(position->numbers[0], position->numbers[1]);
    }

    out = Door{};
    out.closed = Units::ToWorld(at.x, at.y);
    // Up the screen, which is -y in the remake's pixels (behaviours.gd:145).
    out.open = Units::ToWorld(at.x, at.y - kDoorRisePx);
    // Floored at 10 ms, as behaviours.gd floors it (:147).
    out.durationS = static_cast<float>(std::max(milliseconds / 1000.0, 0.01));
    return true;
}

} // namespace MagicPortals::Mover
