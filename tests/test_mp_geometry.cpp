// The level builder and the S2 route, against hand-written shapes.
//
// The engine builds convex hulls from mesh files only, so a 2D collision
// polygon reaches its solver as an OBJ prism (Prism.hpp). What makes that route
// trustworthy can be checked: the prism of a convex polygon is convex, so its
// hull must invent no volume, and its volume must be the polygon's area times
// its depth. Both are asserted here on shapes written for the test;
// test_mp_levels asserts them for every polygon the converter wrote.
//
// Then LevelBuilder, on a hand-written scene: every body type the converter
// writes and where it lands in the engine's space, and the mutation the spike's
// landing check leans on - no statics, nothing to land on.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "core/ConvexHullCache.hpp"
#include "core/PhysicsSettings.hpp"
#include "core/PhysicsSystem.hpp"
#include "sim/LevelBuilder.hpp"
#include "sim/Prism.hpp"
#include "sim/Tscn.hpp"
#include "sim/Units.hpp"

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace MagicPortals;
using namespace Supersonic;

namespace {

const std::filesystem::path kDirectory = std::filesystem::temp_directory_path() / "supersonic-test-mp-geometry";

// A chamfered box shaped like the converter's platforms but not one of them:
// 100 x 40 px with 5 x 4 px corners cut, listed as the converter lists (y down).
const std::vector<double> kChamfered = {-45, 20, -50, 16, -50, -16, -45, -20, 45, -20, 50, -16, 50, 16, 45, 20};
const std::vector<double> kTriangle = {0, -30, 30, 30, -30, 30};

bool Close(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

std::string Show(double value) {
    return std::to_string(value);
}

void RefusesWhatIsNotAConvexPolygon() {
    std::string error;
    CHECK(Prism::ObjText({1, 2, 3}, 1.0, error).empty() && !error.empty());
    error.clear();
    CHECK(Prism::ObjText({0, 0, 1, 1}, 1.0, error).empty() && !error.empty());
    error.clear();
    CHECK(Prism::ObjText(kChamfered, 0.0, error).empty() && error.find("depth") != std::string::npos);
    error.clear();
    // An arrowhead: the fourth point is a reflex corner the hull would fill in.
    CHECK(Prism::ObjText({0, 0, 40, 0, 40, 40, 20, 10, 0, 40}, 1.0, error).empty() &&
          error.find("convex") != std::string::npos);
    error.clear();
    CHECK(Prism::ObjText({0, 0, 10, 0, 20, 0}, 1.0, error).empty() && error.find("area") != std::string::npos);
}

void AreasAreThePolygons() {
    CHECK(Close(Prism::AreaPx(kChamfered), 3960.0, 1e-9)); // 100 x 40, less four 5 x 4 corners
    CHECK(Close(Prism::AreaPx(kTriangle), 1800.0, 1e-9));
}

void ChamferedPrismIsItsOwnHull() {
    std::string error;
    const std::string path = Prism::Write(kChamfered, 2.0, kDirectory, error);
    CHECK_MSG(!path.empty(), error);
    ConvexHullCache cache;
    const ConvexDecomposition* hull = path.empty() ? nullptr : cache.Get("", path);
    CHECK(hull != nullptr);
    if (hull == nullptr) return;

    const double volume = Prism::AreaPx(kChamfered) / (Units::kPixelsPerMetre * Units::kPixelsPerMetre) * 2.0;
    CHECK_EQ(hull->pieces().size(), std::size_t{1});
    CHECK_EQ(hull->pieces().front().vertices().size(), std::size_t{16});
    CHECK_MSG(Close(hull->meshVolume(), volume, 1e-4 * volume),
              "mesh volume " + Show(hull->meshVolume()) + ", want " + Show(volume));
    CHECK_MSG(hull->invented() <= 1e-4 * volume, "invented " + Show(hull->invented()));
    CHECK(hull->pieces().front().IsConvex() && hull->pieces().front().SatisfiesEulerFormula());

    // A metre either side, 0.4 m up and down, a metre deep either side.
    CHECK(Close(hull->boundsMin().x, -1.0, 1e-6) && Close(hull->boundsMax().x, 1.0, 1e-6));
    CHECK(Close(hull->boundsMin().y, -0.4, 1e-6) && Close(hull->boundsMax().y, 0.4, 1e-6));
    CHECK(Close(hull->boundsMin().z, -1.0, 1e-6) && Close(hull->boundsMax().z, 1.0, 1e-6));
}

void TriangleToo() {
    std::string error;
    const std::string path = Prism::Write(kTriangle, 1.0, kDirectory, error);
    ConvexHullCache cache;
    const ConvexDecomposition* hull = path.empty() ? nullptr : cache.Get("", path);
    const double volume = Prism::AreaPx(kTriangle) / (Units::kPixelsPerMetre * Units::kPixelsPerMetre);
    CHECK_MSG(hull && hull->pieces().size() == 1 && hull->pieces().front().vertices().size() == 6, path);
    CHECK(hull && Close(hull->meshVolume(), volume, 1e-4 * volume) && hull->invented() <= 1e-4 * volume);
    // Apex up: the triangle's first point is its top, at y = -30 px - so +0.6 m.
    CHECK(hull && Close(hull->boundsMax().y, 0.6, 1e-6) && Close(hull->boundsMin().y, -0.6, 1e-6));
}

// Either winding is one prism, and one file.
void WindingDoesNotMatter() {
    std::vector<double> reversed;
    for (std::size_t i = kChamfered.size(); i >= 2; i -= 2) {
        reversed.push_back(kChamfered[i - 2]);
        reversed.push_back(kChamfered[i - 1]);
    }
    std::string error;
    const std::string a = Prism::Write(kChamfered, 2.0, kDirectory, error);
    const std::string b = Prism::Write(reversed, 2.0, kDirectory, error);
    CHECK_MSG(!a.empty() && a == b, a + " vs " + b);
    CHECK(Prism::Write(kChamfered, 1.0, kDirectory, error) != a); // another depth is another prism
}

// The whole route: a box dropped on a prism comes to rest on its top face -
// and, as the mutation, falls straight through where the prism is not.
void BoxRestsOnAPrism() {
    std::string error;
    const std::string path = Prism::Write(kChamfered, 2.0, kDirectory, error);
    for (bool withLedge : {true, false}) {
        entt::registry registry;
        PhysicsSettings settings;
        settings.gravity = glm::vec3(0.0f, -static_cast<float>(Units::kGravity), 0.0f);
        registry.ctx().insert_or_assign<PhysicsSettings>(std::move(settings));

        if (withLedge) {
            const auto ledge = registry.create();
            registry.emplace<TransformComponent>(ledge);
            registry.emplace<ConvexHullColliderComponent>(ledge).sourcePath = path;
        }
        // Something solid far away, so the step has two colliders either way.
        const auto far = registry.create();
        registry.emplace<TransformComponent>(far).position = glm::vec3(100.0f, 0.0f, 0.0f);
        registry.emplace<BoxColliderComponent>(far);

        const auto box = registry.create();
        registry.emplace<TransformComponent>(box).position = glm::vec3(0.0f, 2.0f, 0.0f);
        registry.emplace<BoxColliderComponent>(box).size = glm::vec3(0.4f, 0.4f, 1.0f);
        registry.emplace<RigidBodyComponent>(box);

        for (int i = 0; i < 180; ++i) PhysicsSystem::Update(registry, 1.0f / 60.0f);
        const float y = registry.get<TransformComponent>(box).position.y;
        if (withLedge) {
            CHECK_MSG(Close(y, 0.6, 0.02), "rests on the top face (0.4 m) at 0.6 m: y = " + Show(y));
            CHECK(PhysicsSystem::IsGrounded(registry, glm::vec3(0.0f, y - 0.18f, 0.0f), 0.05f, box));
        } else {
            CHECK_MSG(y < -1.0f, "falls through where the prism is not: y = " + Show(y));
        }
    }
}

const std::string kLevel = R"([gd_scene load_steps=4 format=3]

[sub_resource type="RectangleShape2D" id="R"]
size = Vector2(100, 50)

[sub_resource type="CircleShape2D" id="C"]
radius = 25

[sub_resource type="RectangleShape2D" id="S"]
size = Vector2(20, 30)

[node name="lvl" type="Node2D"]

[node name="wall" type="Node2D" parent="."]
position = Vector2(100, 200)
rotation = 1.5708

[node name="Body" type="StaticBody2D" parent="wall"]

[node name="Shape" type="CollisionShape2D" parent="wall/Body"]
position = Vector2(10, -5)
shape = SubResource("R")

[node name="ledge" type="Node2D" parent="."]
position = Vector2(300, 400)

[node name="Body" type="StaticBody2D" parent="ledge"]

[node name="Shape" type="CollisionPolygon2D" parent="ledge/Body"]
polygon = PackedVector2Array(-45, 20, -50, 16, -50, -16, -45, -20, 45, -20, 50, -16, 50, 16, 45, 20)

[node name="ball" type="Node2D" parent="."]
position = Vector2(50, 50)

[node name="Body" type="RigidBody2D" parent="ball"]

[node name="Shape" type="CollisionShape2D" parent="ball/Body"]
shape = SubResource("C")

[node name="pad" type="Node2D" parent="."]
position = Vector2(0, 100)
metadata/trigger_size = Vector2(8, 24)
metadata/trigger_offset = Vector2(0, 37)

[node name="Body" type="Area2D" parent="pad"]

[node name="Shape" type="CollisionShape2D" parent="pad/Body"]
shape = SubResource("S")

[node name="sign" type="Node2D" parent="."]
position = Vector2(5, 5)
)";

void BuilderPlacesEveryBodyType() {
    Tscn::Scene scene;
    std::string error;
    CHECK_MSG(Tscn::Parse(kLevel, scene, error), error);

    entt::registry registry;
    LevelBuilder::Options options;
    options.prismDirectory = kDirectory;
    LevelBuilder::Built built;
    const bool ok = LevelBuilder::Build(scene, registry, options, built, error);
    CHECK_MSG(ok, error);
    if (!ok) return;

    CHECK_EQ(built.statics, 2);
    CHECK_EQ(built.rigids, 1);
    CHECK_EQ(built.areas, 1);
    CHECK_EQ(built.hulls, 1);
    CHECK_EQ(built.entities.size(), std::size_t{4});
    CHECK(built.entities.count("sign") == 0); // no Body, no body

    // Position and rotation flip with y; the shape's own offset is the
    // collider's centre, so the entity's rotation turns it.
    const entt::entity wall = built.entities.at("wall");
    const auto& transform = registry.get<TransformComponent>(wall);
    CHECK(Close(transform.position.x, 2.0, 1e-6) && Close(transform.position.y, -4.0, 1e-6) &&
          transform.position.z == 0.0f);
    CHECK(Close(transform.rotation.z, -1.5708, 1e-6));
    const auto* box = registry.try_get<BoxColliderComponent>(wall);
    CHECK(box && Close(box->size.x, 2.0, 1e-6) && Close(box->size.y, 1.0, 1e-6) &&
          Close(box->size.z, LevelBuilder::kStaticDepthMetres, 1e-6));
    CHECK(box && Close(box->center.x, 0.2, 1e-6) && Close(box->center.y, 0.1, 1e-6) && !box->isTrigger);
    CHECK(!registry.all_of<RigidBodyComponent>(wall));
    // The oracle's material, not the engine's fallback for a collider with no body.
    const auto* surface = registry.try_get<PhysicsMaterialComponent>(wall);
    CHECK(surface && surface->friction == LevelBuilder::kStaticFriction &&
          surface->restitution == LevelBuilder::kStaticRestitution);
    CHECK(registry.get<TagComponent>(wall).tag == "wall");

    const auto* hull = registry.try_get<ConvexHullColliderComponent>(built.entities.at("ledge"));
    CHECK(hull && std::filesystem::exists(hull->sourcePath));
    CHECK(hull && hull->sourcePath == Prism::Write(kChamfered, LevelBuilder::kStaticDepthMetres, kDirectory, error));

    const entt::entity ball = built.entities.at("ball");
    const auto* sphere = registry.try_get<SphereColliderComponent>(ball);
    CHECK(sphere && Close(sphere->radius, 0.5, 1e-6));
    const auto* rigid = registry.try_get<RigidBodyComponent>(ball);
    CHECK(rigid && rigid->friction == LevelBuilder::kBodyFriction && rigid->restitution == LevelBuilder::kBodyRestitution);
    CHECK(rigid && rigid->lockPosition == LevelBuilder::kPlaneLockPosition &&
          rigid->lockRotation == LevelBuilder::kPlaneLockRotation);

    const auto* pad = registry.try_get<BoxColliderComponent>(built.entities.at("pad"));
    CHECK(pad && pad->isTrigger && Close(pad->size.x, 0.4, 1e-6) && Close(pad->size.y, 0.6, 1e-6));
    CHECK(!registry.all_of<RigidBodyComponent>(built.entities.at("pad")));

    // The landing mutation: statics left out, everything else as it was.
    entt::registry bare;
    options.withStatics = false;
    options.lockToPlane = false;
    CHECK_MSG(LevelBuilder::Build(scene, bare, options, built, error), error);
    CHECK_EQ(built.statics, 0);
    CHECK_EQ(built.entities.size(), std::size_t{2});
    CHECK(built.entities.count("ball") == 1 && built.entities.count("pad") == 1);
    // The locks are the builder's to give: asked not to, it locks nothing.
    CHECK(!glm::any(bare.get<RigidBodyComponent>(built.entities.at("ball")).lockPosition) &&
          !glm::any(bare.get<RigidBodyComponent>(built.entities.at("ball")).lockRotation));

    // A trigger from metadata: the box LevelRuntime builds, at its offset.
    const entt::entity trigger = LevelBuilder::AddTriggerFromMetadata(scene, "pad", registry, error);
    CHECK(trigger != entt::null);
    const auto* triggerBox = trigger != entt::null ? registry.try_get<BoxColliderComponent>(trigger) : nullptr;
    CHECK(triggerBox && triggerBox->isTrigger && Close(triggerBox->size.x, 0.16, 1e-6) &&
          Close(triggerBox->size.y, 0.48, 1e-6));
    CHECK(triggerBox && Close(triggerBox->center.x, 0.0, 1e-6) && Close(triggerBox->center.y, -0.74, 1e-6));
    CHECK(trigger != entt::null && Close(registry.get<TransformComponent>(trigger).position.y, -2.0, 1e-6));

    error.clear();
    CHECK(LevelBuilder::AddTriggerFromMetadata(scene, "sign", registry, error) == entt::null &&
          error.find("trigger_size") != std::string::npos);
    error.clear();
    CHECK(LevelBuilder::AddTriggerFromMetadata(scene, "ghost", registry, error) == entt::null && !error.empty());
}

// What the builder cannot place, it refuses by name.
void BuilderRefusesWhatItCannotPlace() {
    const std::string head = "[gd_scene format=3]\n\n[sub_resource type=\"RectangleShape2D\" id=\"R\"]\n"
                             "size = Vector2(10, 10)\n\n[node name=\"lvl\" type=\"Node2D\"]\n\n"
                             "[node name=\"a\" type=\"Node2D\" parent=\".\"]\n\n";
    const struct {
        std::string scene;
        std::string needle;
    } cases[] = {
        {head + "[node name=\"Body\" type=\"StaticBody2D\" parent=\"a\"]\n\n"
                "[node name=\"Shape\" type=\"CollisionShape2D\" parent=\"a/Body\"]\nshape = SubResource(\"R\")\n\n"
                "[node name=\"Shape2\" type=\"CollisionShape2D\" parent=\"a/Body\"]\nshape = SubResource(\"R\")\n",
         "2 shapes"},
        {head + "[node name=\"Body\" type=\"Sprite2D\" parent=\"a\"]\n", "Sprite2D"},
        {head + "[node name=\"Body\" type=\"StaticBody2D\" parent=\"a\"]\n\n"
                "[node name=\"Shape\" type=\"CollisionPolygon2D\" parent=\"a/Body\"]\n"
                "polygon = PackedVector2Array(0, 0, 40, 0, 40, 40, 20, 10, 0, 40)\n",
         "convex"},
    };
    for (const auto& c : cases) {
        Tscn::Scene scene;
        std::string error;
        CHECK_MSG(Tscn::Parse(c.scene, scene, error), error);
        entt::registry registry;
        LevelBuilder::Options options;
        options.prismDirectory = kDirectory;
        LevelBuilder::Built built;
        const bool ok = LevelBuilder::Build(scene, registry, options, built, error);
        CHECK_MSG(!ok && error.find(c.needle) != std::string::npos, "wanted '" + c.needle + "', got '" + error + "'");
    }
}

void runTests() {
    RefusesWhatIsNotAConvexPolygon();
    AreasAreThePolygons();
    ChamferedPrismIsItsOwnHull();
    TriangleToo();
    WindingDoesNotMatter();
    BoxRestsOnAPrism();
    BuilderPlacesEveryBodyType();
    BuilderRefusesWhatItCannotPlace();
}

} // namespace

TEST_MAIN("test_mp_geometry", 55)
