#include "sim/LevelBuilder.hpp"

#include "core/Components.hpp"
#include "sim/Prism.hpp"
#include "sim/Units.hpp"

#include <vector>

namespace MagicPortals::LevelBuilder {

using Supersonic::BoxColliderComponent;
using Supersonic::ConvexHullColliderComponent;
using Supersonic::PhysicsMaterialComponent;
using Supersonic::RigidBodyComponent;
using Supersonic::SphereColliderComponent;
using Supersonic::TagComponent;
using Supersonic::TransformComponent;

namespace {

glm::dvec2 VectorOf(const Tscn::Value* v) {
    if (v == nullptr || v->kind != Tscn::Value::Kind::Vector2) return glm::dvec2(0.0);
    return glm::dvec2(v->numbers[0], v->numbers[1]);
}

double NumberOf(const Tscn::Value* v) {
    double x = 0.0;
    return v != nullptr && v->AsNumber(x) ? x : 0.0;
}

std::vector<const Tscn::Node*> ChildrenOf(const Tscn::Scene& scene, const Tscn::Node& parent) {
    std::vector<const Tscn::Node*> children;
    for (const Tscn::Node& node : scene.nodes)
        if (node.parent == parent.path) children.push_back(&node);
    return children;
}

entt::entity Place(entt::registry& registry, const Tscn::Node& node, const std::string& tag) {
    const entt::entity entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    const glm::dvec2 position = VectorOf(node.Find("position"));
    transform.position = Units::ToWorld(position.x, position.y);
    transform.rotation.z = Units::ToWorldRotation(NumberOf(node.Find("rotation")));
    registry.emplace<TagComponent>(entity, TagComponent{tag});
    return entity;
}

bool IsEntityNode(const Tscn::Node& node) { return node.parent == "." && node.type == "Node2D"; }

// One entity node. `built` is false when it is skipped: scenery or a marker (no
// Body), or a static that withStatics leaves out.
bool BuildNode(const Tscn::Scene& scene, const Tscn::Node& node, entt::registry& registry, const Options& options,
               Built& out, bool& built, std::string& error) {
    built = false;
    const Tscn::Node* body = scene.Child(node, "Body");
    if (body == nullptr) return true; // scenery and markers

    const bool isStatic = body->type == "StaticBody2D";
    const bool isRigid = body->type == "RigidBody2D";
    const bool isArea = body->type == "Area2D";
    if (!isStatic && !isRigid && !isArea) {
        error = node.name + ": its Body is a " + body->type;
        return false;
    }
    const std::vector<const Tscn::Node*> shapes = ChildrenOf(scene, *body);
    if (shapes.size() != 1) {
        error = node.name + ": " + std::to_string(shapes.size()) +
                " shapes under its Body - the builder takes exactly one, which is every body the converter writes";
        return false;
    }
    if (isStatic && !options.withStatics) return true;
    const bool isMover =
        isStatic && options.roles != nullptr && Roles::Moves(Roles::RoleOf(*options.roles, node));

    const Tscn::Node& shape = *shapes.front();
    const double depth = isRigid ? kBodyDepthMetres : kStaticDepthMetres;
    const glm::dvec2 offset = VectorOf(shape.Find("position"));
    const entt::entity entity = Place(registry, node, node.name);

    if (shape.type == "CollisionPolygon2D") {
        const Tscn::Value* polygon = shape.Find("polygon");
        if (polygon == nullptr) {
            error = node.name + ": a CollisionPolygon2D with no polygon";
            return false;
        }
        std::vector<double> points = polygon->numbers;
        for (std::size_t i = 0; i + 1 < points.size(); i += 2) {
            points[i] += offset.x;
            points[i + 1] += offset.y;
        }
        std::string why;
        const std::string path = Prism::Write(points, depth, options.prismDirectory, why);
        if (path.empty()) {
            error = node.name + ": " + why;
            return false;
        }
        auto& hull = registry.emplace<ConvexHullColliderComponent>(entity);
        hull.sourcePath = path;
        hull.isTrigger = isArea;
        ++out.hulls;
    } else if (shape.type == "CollisionShape2D") {
        const Tscn::Value* ref = shape.Find("shape");
        const Tscn::Resource* resource = ref != nullptr ? scene.Embedded(ref->text) : nullptr;
        if (resource == nullptr) {
            error = node.name + ": a CollisionShape2D with no shape";
            return false;
        }
        const glm::vec3 centre = Units::ToWorld(offset.x, offset.y);
        if (resource->type == "RectangleShape2D") {
            const glm::dvec2 size = VectorOf(resource->Find("size"));
            auto& box = registry.emplace<BoxColliderComponent>(entity);
            box.size = glm::vec3(Units::ToMetres(size.x), Units::ToMetres(size.y), static_cast<float>(depth));
            box.center = centre;
            box.isTrigger = isArea;
        } else if (resource->type == "CircleShape2D") {
            auto& sphere = registry.emplace<SphereColliderComponent>(entity);
            sphere.radius = Units::ToMetres(NumberOf(resource->Find("radius")));
            sphere.center = centre;
            sphere.isTrigger = isArea;
        } else {
            error = node.name + ": a " + resource->type + " is not a collision shape";
            return false;
        }
    } else {
        error = node.name + ": a " + shape.type + " under its Body is not a collision shape";
        return false;
    }

    if (isRigid) {
        auto& rigid = registry.emplace<RigidBodyComponent>(entity);
        rigid.friction = kBodyFriction;
        rigid.restitution = kBodyRestitution;
        if (options.lockToPlane) {
            rigid.lockPosition = kPlaneLockPosition;
            rigid.lockRotation = kPlaneLockRotation;
        }
        ++out.rigids;
    } else if (isMover) {
        // Moved by the port and carrying what stands on it: kinematic, so the
        // contact solve reads the velocity Mover::MoveKinematic writes (F1). A
        // kinematic body is moved by code and reads no locks.
        auto& rigid = registry.emplace<RigidBodyComponent>(entity);
        rigid.isKinematic = true;
        rigid.friction = kBodyFriction;
        rigid.restitution = kBodyRestitution;
        ++out.movers;
    } else if (isArea) {
        ++out.areas;
    } else {
        auto& surface = registry.emplace<PhysicsMaterialComponent>(entity);
        surface.friction = kStaticFriction;
        surface.restitution = kStaticRestitution;
        ++out.statics;
    }
    out.entities[node.name] = entity;
    built = true;
    return true;
}

} // namespace

bool Build(const Tscn::Scene& scene, entt::registry& registry, const Options& options, Built& out,
           std::string& error) {
    out = Built{};
    for (const Tscn::Node& node : scene.nodes) {
        if (!IsEntityNode(node)) continue;
        bool built = false;
        if (!BuildNode(scene, node, registry, options, out, built, error)) return false;
    }
    return true;
}

bool ShapeBoundsPx(const Tscn::Scene& scene, const Tscn::Node& node, glm::dvec2& offsetPx, glm::dvec2& sizePx) {
    const Tscn::Node* body = scene.Child(node, "Body");
    const Tscn::Node* shape = body != nullptr ? scene.Child(*body, "Shape") : nullptr;
    if (shape == nullptr) return false;
    offsetPx = VectorOf(shape->Find("position"));
    if (shape->type == "CollisionPolygon2D") {
        const Tscn::Value* polygon = shape->Find("polygon");
        if (polygon == nullptr || polygon->numbers.size() < 2) return false;
        glm::dvec2 lo(polygon->numbers[0], polygon->numbers[1]);
        glm::dvec2 hi = lo;
        for (std::size_t i = 0; i + 1 < polygon->numbers.size(); i += 2) {
            const glm::dvec2 point(polygon->numbers[i], polygon->numbers[i + 1]);
            lo = glm::min(lo, point);
            hi = glm::max(hi, point);
        }
        offsetPx += (lo + hi) * 0.5;
        sizePx = hi - lo;
        return true;
    }
    const Tscn::Value* ref = shape->Find("shape");
    const Tscn::Resource* resource = ref != nullptr ? scene.Embedded(ref->text) : nullptr;
    if (resource == nullptr) return false;
    if (resource->type == "RectangleShape2D") {
        const Tscn::Value* size = resource->Find("size");
        if (size == nullptr || size->kind != Tscn::Value::Kind::Vector2) return false;
        sizePx = VectorOf(size);
        return true;
    }
    if (resource->type == "CircleShape2D") {
        double radius = 0.0;
        const Tscn::Value* value = resource->Find("radius");
        if (value == nullptr || !value->AsNumber(radius)) return false;
        sizePx = glm::dvec2(radius * 2.0);
        return true;
    }
    return false;
}

entt::entity BuildEntity(const Tscn::Scene& scene, const std::string& nodeName, entt::registry& registry,
                         const Options& options, Built& out, std::string& error) {
    const Tscn::Node* node = scene.FindNode(nodeName);
    if (node == nullptr || !IsEntityNode(*node)) {
        error = nodeName + " is not an entity of the scene";
        return entt::null;
    }
    bool built = false;
    if (!BuildNode(scene, *node, registry, options, out, built, error)) return entt::null;
    if (!built) {
        error = nodeName + " has no body to build";
        return entt::null;
    }
    return out.entities.at(nodeName);
}

entt::entity AddTriggerFromMetadata(const Tscn::Scene& scene, const std::string& entityName,
                                    entt::registry& registry, std::string& error) {
    const Tscn::Node* node = scene.FindNode(entityName);
    if (node == nullptr) {
        error = entityName + " is not in the scene";
        return entt::null;
    }
    const Tscn::Value* size = node->Meta("trigger_size");
    if (size == nullptr || size->kind != Tscn::Value::Kind::Vector2) {
        error = entityName + " has no trigger_size";
        return entt::null;
    }
    // The Area2D is the node's child at trigger_offset, so the node's rotation
    // turns the offset too - which the collider's centre gets for free.
    const glm::dvec2 offset = VectorOf(node->Meta("trigger_offset"));
    const entt::entity entity = Place(registry, *node, entityName + "/Trigger");
    auto& box = registry.emplace<BoxColliderComponent>(entity);
    box.size = glm::vec3(Units::ToMetres(size->numbers[0]), Units::ToMetres(size->numbers[1]),
                         static_cast<float>(kStaticDepthMetres));
    box.center = Units::ToWorld(offset.x, offset.y);
    box.isTrigger = true;
    return entity;
}

} // namespace MagicPortals::LevelBuilder
