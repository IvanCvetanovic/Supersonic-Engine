#include "sim/Hinge.hpp"

#include "sim/Units.hpp"

#include "core/Components.hpp"
#include "core/Joints.hpp"
#include "core/Json.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace MagicPortals::Hinge {

namespace {

namespace Json = Supersonic::Json;
using Supersonic::TransformComponent;
using Supersonic::WorldTransformComponent;

// The hinge axis: the plane's normal, and the only axis a 2D level can turn
// about. Both ends carry it, because a hinge holds one body's axis to the
// OTHER's and a joint whose two axes are the same vector by construction
// measures nothing.
const glm::vec3 kAxis(0.0f, 0.0f, 1.0f);

// A node's metadata as a number. The converter writes the eight joint_* keys
// BARE (-1.5708) and the original's custom data quoted; AsNumber reads both.
double Meta(const Tscn::Node& node, const char* key, double fallback) {
    const Tscn::Value* value = node.Meta(key);
    double read = 0.0;
    if (value == nullptr || !value->AsNumber(read)) return fallback;
    return read;
}

double RotationOf(const Tscn::Node& node) {
    const Tscn::Value* value = node.Find("rotation");
    double read = 0.0;
    if (value == nullptr || !value->AsNumber(read)) return 0.0;
    return read;
}

bool PositionOf(const Tscn::Node& node, glm::dvec2& out) {
    const Tscn::Value* value = node.Find("position");
    if (value == nullptr || value->kind != Tscn::Value::Kind::Vector2) return false;
    out = glm::dvec2(value->numbers[0], value->numbers[1]);
    return true;
}

// In the level's own space, which turns the other way from the engine's.
glm::dvec2 RotatePx(const glm::dvec2& v, double radians) {
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    return glm::dvec2(v.x * c - v.y * s, v.x * s + v.y * c);
}

// An offset in the level's pixels as one in the engine's metres: y flips with
// the space, exactly as Units::ToWorld does for a point.
glm::vec3 OffsetToEngine(const glm::dvec2& px) {
    return glm::vec3(Units::ToMetres(px.x), Units::ToMetres(-px.y), 0.0f);
}

glm::mat3 RotZ(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return glm::mat3(glm::vec3(c, s, 0.0f), glm::vec3(-s, c, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
}

// The hinge angle for two bodies turned by these amounts, through the solver's
// own function rather than an arithmetic of our own.
double AngleBetween(const glm::mat3& basisA, const glm::mat3& basisB) {
    Supersonic::Joints::Constraint probe;
    const glm::vec3 axis = basisA * kAxis;
    const float length = glm::length(axis);
    probe.axisA = length > 1e-6f ? axis / length : kAxis;
    probe.referenceA = glm::normalize(basisA * Supersonic::Joints::PerpendicularTo(kAxis));
    probe.referenceB = glm::normalize(basisB * Supersonic::Joints::PerpendicularTo(kAxis));
    return static_cast<double>(Supersonic::Joints::HingeAngle(probe));
}

glm::mat3 BasisOf(entt::registry& registry, entt::entity entity) {
    if (const auto* world = registry.try_get<WorldTransformComponent>(entity)) {
        return glm::mat3(world->matrix);
    }
    if (const auto* local = registry.try_get<TransformComponent>(entity)) {
        return glm::mat3(local->getModelMatrix());
    }
    return glm::mat3(1.0f);
}

} // namespace

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    out = Rules{};

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot open";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();

    // Named, not a temporary: Json::Parser holds its text by reference.
    const std::string text = buffer.str();
    Json::Parser parser(text);
    Json::Value root;
    if (!parser.Parse(root)) {
        error = path + ": " + parser.Error();
        return false;
    }

    const Json::Value& joint = root["joint"];
    if (!joint.IsObject()) {
        error = path + ": joint is an object";
        return false;
    }

    Rules read;
    read.stiffness = joint["stiffness"].AsNumber(0.0);
    // Zero takes none of the error out and the hinge drifts apart under load;
    // above one it over-corrects and the body shakes. Either looks like a level
    // that works until something stands on it.
    if (read.stiffness <= 0.0 || read.stiffness > 1.0) {
        error = path + ": joint.stiffness is above zero and at most one";
        return false;
    }

    out = read;
    return true;
}

const Hinged* State::Find(const std::string& name) const {
    for (const Hinged& hinged : hinges) {
        if (hinged.name == name) return &hinged;
    }
    return nullptr;
}

double AngleOf(entt::registry& registry, const Hinged& hinged) {
    if (!registry.valid(hinged.body) || !registry.valid(hinged.anchor)) return 0.0;
    return AngleBetween(BasisOf(registry, hinged.body), BasisOf(registry, hinged.anchor));
}

bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const LevelBuilder::Built& built,
          entt::registry& registry, const Rules& rules, State& out, std::string& error) {
    out = State{};
    out.rules = rules;
    (void)roles; // A hinge is found by its joint data, not by its role.

    // Every top-level node by the entity name it was placed from, which is how
    // revoluteJoint names the other end.
    std::unordered_map<std::string, const Tscn::Node*> byEntityName;
    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;
        const std::string entityName = Roles::EntityName(node);
        if (entityName.empty()) continue;
        byEntityName.emplace(entityName, &node);
    }

    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;
        const Tscn::Value* pinned = node.Meta("revoluteJoint");
        if (pinned == nullptr || pinned->kind != Tscn::Value::Kind::String) continue;

        const auto named = byEntityName.find(pinned->text);
        if (named == byEntityName.end()) {
            error = node.name + " is pinned to '" + pinned->text + "', which the level does not place";
            return false;
        }
        const Tscn::Node& anchorNode = *named->second;

        glm::dvec2 atPx(0.0);
        glm::dvec2 anchorAtPx(0.0);
        if (!PositionOf(node, atPx) || !PositionOf(anchorNode, anchorAtPx)) {
            error = node.name + " or its anchor " + anchorNode.name + " has no position";
            return false;
        }

        // Both bodies have to exist. The anchor is static, so a level started
        // without its statics has none, and the joint is passed over rather than
        // attached to nothing.
        const auto bodyFound = built.entities.find(node.name);
        const auto anchorFound = built.entities.find(anchorNode.name);
        if (bodyFound == built.entities.end() || anchorFound == built.entities.end()) continue;

        Hinged hinged;
        hinged.name = node.name;
        hinged.anchorName = anchorNode.name;
        hinged.body = bodyFound->second;
        hinged.anchor = anchorFound->second;

        const double rotation = RotationOf(node);
        const double anchorRotation = RotationOf(anchorNode);

        // Where the pin is: the body's own attach point, turned with the body.
        // It is (0, 0) in all ten placements, so this is the body's centre, but
        // the arithmetic is here rather than the assumption.
        const glm::dvec2 anchorPx(Meta(node, "joint_anchor_x", 0.0), Meta(node, "joint_anchor_y", 0.0));
        const glm::dvec2 pinPx = atPx + RotatePx(anchorPx, rotation);

        // The same point seen from the anchor: Box2D puts both ends of the joint
        // on one world point, which is why B's dropped attach point costs nothing.
        const glm::dvec2 anchorLocalPx = RotatePx(pinPx - anchorAtPx, -anchorRotation);

        hinged.limited = Meta(node, "joint_enable_limit", 0.0) != 0.0;
        hinged.lowerRad = Meta(node, "joint_lower_angle", 0.0);
        hinged.upperRad = Meta(node, "joint_upper_angle", 0.0);
        hinged.restRad = AngleBetween(RotZ(Units::ToWorldRotation(rotation)),
                                      RotZ(Units::ToWorldRotation(anchorRotation)));
        // ADDED, which is what the arithmetic said in the first place. A body
        // turned about the engine's +z RAISES Joints::HingeAngle - read six ticks
        // in, before it can reach anything - so the engine's sense runs with the
        // original's and both bounds keep their places.
        //
        // This was briefly negated instead, on the strength of a reading taken
        // three seconds into a swing with gravity still on: by then gravity had
        // pulled the bar down past rest, which is indistinguishable from an
        // inverted sign. Hinge.hpp keeps the whole story.
        hinged.minRad = hinged.restRad + hinged.lowerRad;
        hinged.maxRad = hinged.restRad + hinged.upperRad;

        Supersonic::JointComponent joint;
        joint.type = Supersonic::JointComponent::Type::Hinge;
        joint.connectedBody = hinged.anchor;
        joint.anchor = OffsetToEngine(RotatePx(anchorPx, 0.0));
        joint.connectedAnchor = OffsetToEngine(anchorLocalPx);
        joint.axis = kAxis;
        joint.connectedAxis = kAxis;
        joint.useLimit = hinged.limited;
        joint.minAngle = static_cast<float>(hinged.minRad);
        joint.maxAngle = static_cast<float>(hinged.maxRad);
        // Off everywhere, and held off by the original's own script besides.
        joint.useMotor = false;
        joint.stiffness = static_cast<float>(rules.stiffness);
        joint.enabled = true;
        registry.emplace_or_replace<Supersonic::JointComponent>(hinged.body, joint);

        out.hinges.push_back(hinged);
    }
    return true;
}

} // namespace MagicPortals::Hinge
