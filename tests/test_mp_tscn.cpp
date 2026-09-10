// The level reader, against hand-written scenes.
//
// Every scene here is written for the test - none of it is the original game's
// data - so the suite runs on any machine, with or without the converted
// levels. test_mp_levels is the other half and reads the real ones.
//
// Most of it is refusals. The reader's claim is that it reads the converter's
// subset of the format and fails loudly on anything outside it, and a refusal is
// only evidence if it is for the right reason: each rejected scene names the
// line it must be caught on and a word the error must contain.

#include "TestHarness.hpp"

#include "sim/Tscn.hpp"

#include <algorithm>
#include <initializer_list>
#include <string>

using namespace MagicPortals;
using Kind = Tscn::Value::Kind;

namespace {

// Every form in the subset, once each. 1 external + 3 embedded + 1 = 5 steps.
const std::string kScene = R"([gd_scene load_steps=5 format=3]

[ext_resource type="Texture2D" path="res://assets/a.png" id="tex_0"]

[sub_resource type="RectangleShape2D" id="Rect_1"]
size = Vector2(40, 20)

[sub_resource type="CircleShape2D" id="Circle_1"]
radius = 9

[sub_resource type="CanvasItemMaterial" id="Mat_1"]
blend_mode = 1

[node name="root" type="Node2D"]

[node name="thing" type="Node2D" parent="."]
position = Vector2(-12.5, 300)
rotation = 1.5708
z_index = -3
metadata/entity_name = "thing.ent"
metadata/stride = "3000"
metadata/joint_lower_angle = -1.5
metadata/label = "12px"
metadata/trigger_size = Vector2(8, 24)

[node name="Sprite" type="Sprite2D" parent="thing"]
texture = ExtResource("tex_0")
offset = Vector2(0, 16)
material = SubResource("Mat_1")

[node name="Body" type="StaticBody2D" parent="thing"]

[node name="Shape" type="CollisionShape2D" parent="thing/Body"]
shape = SubResource("Rect_1")

[node name="Poly" type="CollisionPolygon2D" parent="thing/Body"]
polygon = PackedVector2Array(-4, 2, 4, 2, 0, -3.25)

[node name="ball" type="Node2D" parent="."]

[node name="Body" type="RigidBody2D" parent="ball"]

[node name="Shape" type="CollisionShape2D" parent="ball/Body"]
shape = SubResource("Circle_1")

[node name="pad" type="Node2D" parent="."]

[node name="Body" type="Area2D" parent="pad"]
)";

// Exact comparison on purpose: from_chars and the compiler both round a decimal
// to the nearest double, so the file's -3.25 and the literal are the same bits.
bool Is(const Tscn::Value* v, Kind kind, std::initializer_list<double> expected) {
    if (v == nullptr || v->kind != kind || v->numbers.size() != expected.size()) return false;
    return std::equal(expected.begin(), expected.end(), v->numbers.begin());
}

bool Ref(const Tscn::Value* v, Kind kind, const std::string& id) {
    return v != nullptr && v->kind == kind && v->text == id;
}

void ReadsEveryForm() {
    Tscn::Scene scene;
    std::string error;
    const bool ok = Tscn::Parse(kScene, scene, error);
    CHECK_MSG(ok, error);
    if (!ok) return;

    CHECK_EQ(scene.format, 3);
    CHECK_EQ(scene.loadSteps, 5);
    CHECK_EQ(scene.external.size(), std::size_t{1});
    CHECK_EQ(scene.embedded.size(), std::size_t{3});
    CHECK_EQ(scene.nodes.size(), std::size_t{11});

    const Tscn::Resource* tex = scene.External("tex_0");
    CHECK(tex && tex->type == "Texture2D" && tex->path == "res://assets/a.png" && tex->properties.empty());
    const Tscn::Resource* rect = scene.Embedded("Rect_1");
    CHECK(rect && rect->type == "RectangleShape2D" && Is(rect->Find("size"), Kind::Vector2, {40, 20}));
    const Tscn::Resource* circle = scene.Embedded("Circle_1");
    CHECK(circle && circle->type == "CircleShape2D" && Is(circle->Find("radius"), Kind::Number, {9}));
    const Tscn::Resource* material = scene.Embedded("Mat_1");
    CHECK(material && Is(material->Find("blend_mode"), Kind::Number, {1}));
    CHECK(scene.Embedded("tex_0") == nullptr); // the two id spaces are separate
    CHECK(scene.External("Rect_1") == nullptr);

    // Paths are Godot's own: "." is the root, a child spells its parent's path.
    const Tscn::Node* root = scene.FindNode(".");
    CHECK(root && root->name == "root" && root->parent.empty() && root->type == "Node2D");
    const Tscn::Node* thing = scene.FindNode("thing");
    CHECK(thing && thing->parent == "." && thing->line == 16);
    if (!thing) return;
    CHECK(Is(thing->Find("position"), Kind::Vector2, {-12.5, 300}));
    CHECK(Is(thing->Find("rotation"), Kind::Number, {1.5708}));
    CHECK(Is(thing->Find("z_index"), Kind::Number, {-3}));
    CHECK(thing->Find("scale") == nullptr);

    const Tscn::Node* body = scene.Child(*thing, "Body");
    CHECK(body && body->type == "StaticBody2D" && body->path == "thing/Body" && body->parent == "thing");
    const Tscn::Node* shape = body ? scene.Child(*body, "Shape") : nullptr;
    CHECK(shape && shape->type == "CollisionShape2D" && Ref(shape->Find("shape"), Kind::SubResource, "Rect_1"));
    const Tscn::Node* poly = scene.FindNode("thing/Body/Poly");
    CHECK(poly && poly->type == "CollisionPolygon2D");
    CHECK(poly && Is(poly->Find("polygon"), Kind::Points, {-4, 2, 4, 2, 0, -3.25}));

    const Tscn::Node* sprite = scene.FindNode("thing/Sprite");
    CHECK(sprite && Ref(sprite->Find("texture"), Kind::ExtResource, "tex_0"));
    CHECK(sprite && Ref(sprite->Find("material"), Kind::SubResource, "Mat_1"));
    CHECK(sprite && Is(sprite->Find("offset"), Kind::Vector2, {0, 16}));

    // Same-named children of different parents are different nodes.
    const Tscn::Node* ball = scene.FindNode("ball/Body");
    CHECK(ball && ball->type == "RigidBody2D");
    CHECK(scene.FindNode("ball/Body/Shape") && Ref(scene.FindNode("ball/Body/Shape")->Find("shape"), Kind::SubResource, "Circle_1"));
    CHECK(scene.FindNode("pad/Body") && scene.FindNode("pad/Body")->type == "Area2D");
    CHECK(scene.FindNode("Body") == nullptr);
    CHECK(scene.FindNode("nowhere") == nullptr);
}

// The converter quotes the original's custom data ("3000") and writes the eight
// joint_* keys bare (-1.5). The remake's meta_float goes through String() and so
// reads only the first; this reads both, and refuses anything that is not
// wholly a number.
void MetadataReadsBothSpellings() {
    Tscn::Scene scene;
    std::string error;
    CHECK_MSG(Tscn::Parse(kScene, scene, error), error);
    const Tscn::Node* thing = scene.FindNode("thing");
    if (!thing) return;

    double x = 0.0;
    const Tscn::Value* stride = thing->Meta("stride");
    CHECK(stride && stride->kind == Kind::String && stride->text == "3000");
    CHECK(stride && stride->AsNumber(x) && x == 3000.0);

    x = 0.0;
    const Tscn::Value* lower = thing->Meta("joint_lower_angle");
    CHECK(lower && lower->kind == Kind::Number);
    CHECK(lower && lower->AsNumber(x) && x == -1.5);

    CHECK(thing->Meta("label") && !thing->Meta("label")->AsNumber(x));
    CHECK(thing->Meta("entity_name") && thing->Meta("entity_name")->text == "thing.ent");
    CHECK(thing->Meta("entity_name") && !thing->Meta("entity_name")->AsNumber(x));
    CHECK(thing->Meta("trigger_size") && !thing->Meta("trigger_size")->AsNumber(x));
    CHECK(Is(thing->Meta("trigger_size"), Kind::Vector2, {8, 24}));

    // Meta() is the metadata/ namespace only, and Find() sees it by full key.
    CHECK(thing->Meta("missing") == nullptr);
    CHECK(thing->Meta("position") == nullptr);
    CHECK(thing->Find("metadata/stride") == stride);
}

// The converted levels are written on Windows with CRLF endings. A reader that
// kept the \r would carry it into every string and every last number on a line.
void CrlfReadsTheSame() {
    std::string crlf;
    for (char c : kScene) {
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    Tscn::Scene scene;
    std::string error;
    CHECK_MSG(Tscn::Parse(crlf, scene, error), error);
    CHECK_EQ(scene.nodes.size(), std::size_t{11});
    const Tscn::Node* thing = scene.FindNode("thing");
    CHECK(thing && thing->Meta("entity_name") && thing->Meta("entity_name")->text == "thing.ent");
    CHECK(scene.FindNode("thing/Body/Poly") &&
          Is(scene.FindNode("thing/Body/Poly")->Find("polygon"), Kind::Points, {-4, 2, 4, 2, 0, -3.25}));
    CHECK(scene.Embedded("Circle_1") && Is(scene.Embedded("Circle_1")->Find("radius"), Kind::Number, {9}));
}

void ParseReplacesTheScene() {
    Tscn::Scene scene;
    std::string error;
    CHECK(Tscn::Parse(kScene, scene, error));
    CHECK(Tscn::Parse(kScene, scene, error));
    CHECK_EQ(scene.nodes.size(), std::size_t{11});
    CHECK_EQ(scene.embedded.size(), std::size_t{3});
}

// Parses `text` and expects it refused at `where` ("line 4", "end of file") for
// a reason whose message contains `needle`.
void Rejects(const std::string& text, const std::string& where, const std::string& needle) {
    Tscn::Scene scene;
    std::string error;
    const bool ok = Tscn::Parse(text, scene, error);
    CHECK_MSG(!ok, "accepted a scene that should fail on: " + needle);
    const std::string prefix = where + ": ";
    CHECK_MSG(error.rfind(prefix, 0) == 0, "expected '" + prefix + "...', got '" + error + "'");
    CHECK_MSG(error.find(needle) != std::string::npos, "expected '" + needle + "' in '" + error + "'");
}

// Three lines every refusal below is appended to, so the offending line is 4.
const std::string kHead = "[gd_scene format=3]\n\n[node name=\"root\" type=\"Node2D\"]\n";

// Outside the subset: things Godot reads and this does not, by design.
void RefusesWhatTheConverterDoesNotWrite() {
    // A joint as a node. The converter carries hinges as joint_* metadata, and a
    // reader that skipped one of these would drop a hinge without a word.
    Rejects(kHead + "[node name=\"j\" type=\"PinJoint2D\" parent=\".\"]\n", "line 4", "PinJoint2D");
    Rejects(kHead + "[connection signal=\"a\" from=\".\" to=\".\" method=\"m\"]\n", "line 4", "[connection]");
    Rejects(kHead + "scale = Vector2(2, 2)\n", "line 4", "'scale'");
    Rejects(kHead + "position = Color(1, 0, 0, 1)\n", "line 4", "Color");
    Rejects(kHead + "position = Vector2i(1, 2)\n", "line 4", "Vector2i");
    Rejects(kHead + "[ext_resource type=\"AudioStream\" path=\"res://a.ogg\" id=\"a\"]\n", "line 4", "AudioStream");
    Rejects(kHead + "[sub_resource type=\"CapsuleShape2D\" id=\"c\"]\n", "line 4", "CapsuleShape2D");
    Rejects(kHead + "[node name=\"n\" parent=\".\" instance=ExtResource(\"x\") type=\"Node2D\"]\n", "line 4", "'instance'");
    Rejects("[gd_scene load_steps=2 format=2]\n", "line 1", "format 2");
    Rejects("[gd_scene format=3]\n[sub_resource type=\"RectangleShape2D\" id=\"r\"]\ncustom_solver_bias = 0.5\n",
            "line 3", "'custom_solver_bias'");
    Rejects(kHead + "metadata/ = 1\n", "line 4", "'metadata/'");
    Rejects(kHead + "metadata/name = \"a\\\"b\"\n", "line 4", "escape");
}

// Broken files, and references that point nowhere.
void RefusesMalformed() {
    Rejects("", "end of file", "no [gd_scene]");
    Rejects("[gd_scene format=3]\n", "end of file", "no nodes");
    Rejects("[node name=\"root\" type=\"Node2D\"]\n", "line 1", "open with [gd_scene]");
    Rejects("z_index = 1\n", "line 1", "open with [gd_scene]");
    Rejects(kHead + "[gd_scene format=3]\n", "line 4", "first section");
    Rejects("[gd_scene format=3 format=3]\n", "line 1", "twice");
    Rejects(kHead + "[node name=\"x\" type=\"Node2D\" parent=\".\"\n", "line 4", "not closed");
    Rejects(kHead + "[node name=\"x\" parent=\".\"]\n", "line 4", "missing type");
    Rejects(kHead + "garbage\n", "line 4", "key = value");

    Rejects(kHead + "[node name=\"other\" type=\"Node2D\"]\n", "line 4", "second root");
    Rejects("[gd_scene format=3]\n[node name=\"r\" type=\"Node2D\" parent=\".\"]\n", "line 2", "root");
    Rejects(kHead + "[node name=\"b\" type=\"Node2D\" parent=\"ghost\"]\n", "line 4", "\"ghost\"");
    Rejects(kHead + "[node name=\"a/b\" type=\"Node2D\" parent=\".\"]\n", "line 4", "plain name");
    Rejects(kHead + "[node name=\"a\" type=\"Node2D\" parent=\".\"]\n\n[node name=\"a\" type=\"Node2D\" parent=\".\"]\n",
            "line 6", "declared twice");
    Rejects("[gd_scene format=3]\n[sub_resource type=\"CircleShape2D\" id=\"c\"]\n"
            "[sub_resource type=\"CircleShape2D\" id=\"c\"]\n",
            "line 3", "declared twice");

    Rejects(kHead + "[node name=\"b\" type=\"CollisionShape2D\" parent=\".\"]\nshape = SubResource(\"nope\")\n",
            "line 5", "\"nope\"");
    Rejects(kHead + "[node name=\"s\" type=\"Sprite2D\" parent=\".\"]\ntexture = ExtResource(\"tex_9\")\n",
            "line 5", "\"tex_9\"");
    Rejects("[gd_scene format=3]\n[ext_resource type=\"Texture2D\" path=\"p\" id=\"t\"]\npath = \"x\"\n",
            "line 3", "takes none");

    Rejects(kHead + "z_index = 1\nz_index = 2\n", "line 5", "set twice");
    Rejects(kHead + "z_index = 12px\n", "line 4", "'12px'");
    Rejects(kHead + "position = Vector2(1, 2, 3)\n", "line 4", "two numbers");
    Rejects(kHead + "position = Vector2(1, 2,)\n", "line 4", "not a number");
    Rejects(kHead + "position = Vector2(1, 2\n", "line 4", "not closed");
    Rejects(kHead + "[node name=\"p\" type=\"CollisionPolygon2D\" parent=\".\"]\npolygon = PackedVector2Array(1, 2, 3)\n",
            "line 5", "odd count");
    Rejects(kHead + "metadata/name = \"abc\n", "line 4", "unterminated");
    Rejects(kHead + "metadata/name = \"a\" \"b\"\n", "line 4", "after a string");
}

void LoadNamesTheFile() {
    Tscn::Scene scene;
    std::string error;
    CHECK(!Tscn::Load("no/such/level.tscn", scene, error));
    CHECK_MSG(error.find("no/such/level.tscn") != std::string::npos, error);
}

void runTests() {
    ReadsEveryForm();
    MetadataReadsBothSpellings();
    CrlfReadsTheSame();
    ParseReplacesTheScene();
    RefusesWhatTheConverterDoesNotWrite();
    RefusesMalformed();
    LoadNamesTheFile();
}

} // namespace

TEST_MAIN("test_mp_tscn", 160)
