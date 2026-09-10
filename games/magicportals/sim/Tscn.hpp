#pragma once

// A reader for the scenes the remake's converter writes - and only those.
//
// Magic Portals' levels exist as Godot 4 text scenes: the remake's
// tools/converter decodes the original's .esc files and writes one .tscn per
// level. The port reads those files rather than a format of its own, for two
// reasons. The remake's repository is an oracle here, not a workspace, so its
// converter does not get a second emitter. And the converter is itself under
// test by the remake's verify_gameplay; a level that came out wrong through a
// new emitter could not be told apart from a level the port got wrong.
//
// It is a SUBSET reader, and strict about it. Everything the converter writes
// across all 128 levels is listed below; anything else - a node type, section,
// attribute, property or value form not listed - is an error naming the line,
// never something skipped. A silently dropped CollisionPolygon2D gives a level
// whose platforms do not collide, and a reader that shrugged at one would hand
// the level builder exactly that.
//
//   sections     gd_scene (format 3), ext_resource, sub_resource, node
//   node types   Node2D, Sprite2D, StaticBody2D, RigidBody2D, Area2D,
//                CollisionShape2D, CollisionPolygon2D
//   resources    Texture2D (external); RectangleShape2D, CircleShape2D,
//                CanvasItemMaterial (embedded)
//   properties   nodes: position, rotation, z_index, texture, offset, material,
//                shape, polygon, metadata/*; resources: size, radius, blend_mode
//   values       "string", number, Vector2(x, y), PackedVector2Array(...),
//                ExtResource("id"), SubResource("id")
//
// Metadata arrives in two spellings. The converter quotes the original's custom
// data ("3000", "1") and writes the eight joint_* keys bare (-1.5). The remake's
// own reader, meta_float in behaviours.gd, goes through String() and so fits
// only the first; Value::AsNumber reads both, and test_mp_tscn pins that it does.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace MagicPortals::Tscn {

// One right-hand side. Which fields mean anything depends on the kind.
struct Value {
    enum class Kind { String, Number, Vector2, Points, ExtResource, SubResource };

    Kind kind = Kind::String;
    std::string text;            // String: the contents. Ext/SubResource: the id.
    std::vector<double> numbers; // Number: 1. Vector2: x, y. Points: x0, y0, x1, y1...

    // A number however the converter spelled it: bare, or a string that is
    // wholly a number. False for anything else.
    bool AsNumber(double& out) const;
};

struct Property {
    std::string key;
    Value value;
};

// [ext_resource] and [sub_resource] alike. An external one has a path and no
// properties; an embedded one the reverse.
struct Resource {
    std::string type;
    std::string id;
    std::string path;
    std::vector<Property> properties;
    int line = 0;

    const Value* Find(std::string_view key) const;
};

struct Node {
    std::string name;
    std::string type;
    std::string parent; // As written: "." for the root's children, empty for the root.
    std::string path;   // Godot's own: "." for the root, "a", "a/Body", ...
    std::vector<Property> properties;
    int line = 0;

    const Value* Find(std::string_view key) const;
    const Value* Meta(std::string_view key) const; // "metadata/<key>"
};

struct Scene {
    int loadSteps = 0;
    int format = 0;
    std::vector<Resource> external;
    std::vector<Resource> embedded;
    std::vector<Node> nodes; // In file order, which puts every parent before its children.

    const Node* FindNode(std::string_view path) const;
    const Node* Child(const Node& parent, std::string_view name) const;
    const Resource* External(std::string_view id) const;
    const Resource* Embedded(std::string_view id) const;
};

// Replaces `out`. On failure `error` reads "line N: why" (or "end of file: why").
bool Parse(std::string_view text, Scene& out, std::string& error);

// The same, from disk, with the path in front of the error.
bool Load(const std::string& path, Scene& out, std::string& error);

} // namespace MagicPortals::Tscn
