#include "sim/Tscn.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace MagicPortals::Tscn {

namespace {

// Everything the converter writes, counted across all 128 levels on 10 September
// 2026, and nothing else. Growing one of these lists is a decision about the
// port rather than a fix to the reader: whoever adds a type also decides what
// the level builder does with it.
constexpr std::string_view kNodeTypes[] = {
    "Node2D", "Sprite2D", "StaticBody2D", "RigidBody2D", "Area2D",
    "CollisionShape2D", "CollisionPolygon2D",
};
constexpr std::string_view kExternalTypes[] = {"Texture2D"};
constexpr std::string_view kEmbeddedTypes[] = {"RectangleShape2D", "CircleShape2D", "CanvasItemMaterial"};
constexpr std::string_view kNodeKeys[] = {
    "position", "rotation", "z_index", "texture", "offset", "material", "shape", "polygon",
};
constexpr std::string_view kEmbeddedKeys[] = {"size", "radius", "blend_mode"};
constexpr std::string_view kMetaPrefix = "metadata/";

using Attributes = std::vector<std::pair<std::string, std::string>>;

template <std::size_t N>
bool OneOf(std::string_view s, const std::string_view (&set)[N]) {
    return std::find(std::begin(set), std::end(set), s) != std::end(set);
}

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

// The whole of s or nothing. from_chars rather than strtod, which takes its
// decimal point from the C locale.
bool ParseNumber(std::string_view s, double& out) {
    if (s.empty()) return false;
    const char* last = s.data() + s.size();
    const auto [ptr, ec] = std::from_chars(s.data(), last, out);
    return ec == std::errc() && ptr == last && std::isfinite(out);
}

bool ParseInt(std::string_view s, int& out) {
    if (s.empty()) return false;
    const char* last = s.data() + s.size();
    const auto [ptr, ec] = std::from_chars(s.data(), last, out);
    return ec == std::errc() && ptr == last;
}

// A double-quoted string at the front of s. The converter writes no escapes and
// the subset has none: a backslash is an error rather than a guess at Godot's
// escaping rules.
bool ParseQuoted(std::string_view s, std::string& out, std::string_view& rest, std::string& why) {
    if (s.empty() || s.front() != '"') {
        why = "expected a quoted string";
        return false;
    }
    const std::size_t close = s.find('"', 1);
    if (close == std::string_view::npos) {
        why = "unterminated string";
        return false;
    }
    const std::string_view body = s.substr(1, close - 1);
    if (body.find('\\') != std::string_view::npos) {
        why = "escape sequences are not in the subset";
        return false;
    }
    out.assign(body);
    rest = s.substr(close + 1);
    return true;
}

bool ParseValue(std::string_view s, Value& v, std::string& why) {
    s = Trim(s);
    if (s.empty()) {
        why = "missing value";
        return false;
    }

    if (s.front() == '"') {
        std::string_view rest;
        if (!ParseQuoted(s, v.text, rest, why)) return false;
        if (!Trim(rest).empty()) {
            why = "text after a string";
            return false;
        }
        v.kind = Value::Kind::String;
        return true;
    }

    const std::size_t open = s.find('(');
    if (open == std::string_view::npos) {
        double x = 0.0;
        if (!ParseNumber(s, x)) {
            why = "'" + std::string(s) + "' is not a value the subset reads";
            return false;
        }
        v.kind = Value::Kind::Number;
        v.numbers = {x};
        return true;
    }

    const std::string ctor(s.substr(0, open));
    if (s.back() != ')') {
        why = ctor + "(...) is not closed";
        return false;
    }
    const std::string_view args = Trim(s.substr(open + 1, s.size() - open - 2));

    if (ctor == "ExtResource" || ctor == "SubResource") {
        std::string_view rest;
        if (!ParseQuoted(args, v.text, rest, why)) return false;
        if (!Trim(rest).empty()) {
            why = ctor + " takes one id";
            return false;
        }
        v.kind = ctor == "ExtResource" ? Value::Kind::ExtResource : Value::Kind::SubResource;
        return true;
    }

    if (ctor != "Vector2" && ctor != "PackedVector2Array") {
        why = "value type " + ctor + " is not in the subset";
        return false;
    }

    v.numbers.clear();
    if (!args.empty()) {
        std::string_view rest = args;
        while (true) {
            const std::size_t comma = rest.find(',');
            const std::string_view item = Trim(rest.substr(0, comma));
            double x = 0.0;
            if (!ParseNumber(item, x)) {
                why = ctor + ": '" + std::string(item) + "' is not a number";
                return false;
            }
            v.numbers.push_back(x);
            if (comma == std::string_view::npos) break;
            rest = rest.substr(comma + 1);
        }
    }

    if (ctor == "Vector2") {
        if (v.numbers.size() != 2) {
            why = "Vector2 takes two numbers, got " + std::to_string(v.numbers.size());
            return false;
        }
        v.kind = Value::Kind::Vector2;
    } else {
        if (v.numbers.size() % 2 != 0) {
            why = "PackedVector2Array has an odd count of numbers (" + std::to_string(v.numbers.size()) + ")";
            return false;
        }
        v.kind = Value::Kind::Points;
    }
    return true;
}

const std::string* Attribute(const Attributes& attributes, std::string_view key) {
    for (const auto& attribute : attributes)
        if (attribute.first == key) return &attribute.second;
    return nullptr;
}

// [tag key="value" key=value ...]
bool ParseHeader(std::string_view line, std::string& tag, Attributes& attributes, std::string& why) {
    if (line.back() != ']') {
        why = "section header is not closed";
        return false;
    }
    std::string_view s = Trim(line.substr(1, line.size() - 2));
    const std::size_t space = s.find(' ');
    tag.assign(s.substr(0, space));
    s = space == std::string_view::npos ? std::string_view{} : Trim(s.substr(space));

    attributes.clear();
    while (!s.empty()) {
        const std::size_t eq = s.find('=');
        if (eq == std::string_view::npos || eq == 0) {
            why = "malformed attribute in [" + tag + "]";
            return false;
        }
        std::string key(s.substr(0, eq));
        s = s.substr(eq + 1);

        std::string value;
        if (!s.empty() && s.front() == '"') {
            std::string_view rest;
            if (!ParseQuoted(s, value, rest, why)) return false;
            s = rest;
        } else {
            const std::size_t end = s.find(' ');
            value.assign(s.substr(0, end));
            s = end == std::string_view::npos ? std::string_view{} : s.substr(end);
        }
        if (!s.empty() && s.front() != ' ') {
            why = "malformed attribute '" + key + "' in [" + tag + "]";
            return false;
        }
        s = Trim(s);

        if (Attribute(attributes, key) != nullptr) {
            why = "attribute '" + key + "' given twice";
            return false;
        }
        attributes.emplace_back(std::move(key), std::move(value));
    }
    return true;
}

// Every attribute must be one the section takes, and every required one there.
bool CheckAttributes(const std::string& tag, const Attributes& attributes,
                     std::initializer_list<std::string_view> allowed,
                     std::initializer_list<std::string_view> required, std::string& why) {
    for (const auto& attribute : attributes) {
        if (std::find(allowed.begin(), allowed.end(), attribute.first) == allowed.end()) {
            why = "[" + tag + "] attribute '" + attribute.first + "' is not in the subset";
            return false;
        }
    }
    for (std::string_view key : required) {
        if (Attribute(attributes, key) == nullptr) {
            why = "[" + tag + "] is missing " + std::string(key);
            return false;
        }
    }
    return true;
}

const Value* FindIn(const std::vector<Property>& properties, std::string_view key) {
    for (const Property& property : properties)
        if (property.key == key) return &property.value;
    return nullptr;
}

} // namespace

bool Value::AsNumber(double& out) const {
    if (kind == Kind::Number) {
        out = numbers.front();
        return true;
    }
    return kind == Kind::String && ParseNumber(text, out);
}

const Value* Resource::Find(std::string_view key) const {
    return FindIn(properties, key);
}

const Value* Node::Find(std::string_view key) const {
    return FindIn(properties, key);
}

const Value* Node::Meta(std::string_view key) const {
    for (const Property& property : properties) {
        if (property.key.size() == kMetaPrefix.size() + key.size() &&
            property.key.starts_with(kMetaPrefix) && property.key.ends_with(key))
            return &property.value;
    }
    return nullptr;
}

const Node* Scene::FindNode(std::string_view path) const {
    for (const Node& node : nodes)
        if (node.path == path) return &node;
    return nullptr;
}

const Node* Scene::Child(const Node& parent, std::string_view name) const {
    if (parent.path == ".") return FindNode(name);
    return FindNode(parent.path + "/" + std::string(name));
}

const Resource* Scene::External(std::string_view id) const {
    for (const Resource& resource : external)
        if (resource.id == id) return &resource;
    return nullptr;
}

const Resource* Scene::Embedded(std::string_view id) const {
    for (const Resource& resource : embedded)
        if (resource.id == id) return &resource;
    return nullptr;
}

bool Parse(std::string_view text, Scene& out, std::string& error) {
    out = Scene{};

    enum class Section { None, Scene, External, Embedded, Node };
    Section section = Section::None;
    // Where the current section's properties go. Reset at every header and set
    // only after that header's push_back, so a reallocation never leaves it
    // pointing into a moved vector.
    std::vector<Property>* target = nullptr;
    std::unordered_map<std::string, std::size_t> nodeByPath;
    std::unordered_set<std::string> externalIds;
    std::unordered_set<std::string> embeddedIds;
    std::unordered_set<std::string> keysHere;

    int lineNo = 0;
    std::string why;
    const auto fail = [&](const std::string& message) {
        error = "line " + std::to_string(lineNo) + ": " + message;
        return false;
    };

    for (std::size_t pos = 0; pos < text.size();) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        ++lineNo;
        // The converted levels are written on Windows with CRLF endings.
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (Trim(line).empty()) continue;

        if (line.front() == '[') {
            std::string tag;
            Attributes attributes;
            if (!ParseHeader(line, tag, attributes, why)) return fail(why);
            keysHere.clear();
            target = nullptr;

            if (tag == "gd_scene") {
                if (section != Section::None) return fail("[gd_scene] must be the first section, and the only one");
                if (!CheckAttributes(tag, attributes, {"load_steps", "format"}, {"format"}, why)) return fail(why);
                if (const std::string* steps = Attribute(attributes, "load_steps"))
                    if (!ParseInt(*steps, out.loadSteps)) return fail("load_steps '" + *steps + "' is not an integer");
                const std::string& format = *Attribute(attributes, "format");
                if (!ParseInt(format, out.format) || out.format != 3)
                    return fail("format " + format + " - only format 3 (Godot 4) is read");
                section = Section::Scene;
            } else if (section == Section::None) {
                return fail("the file must open with [gd_scene]");
            } else if (tag == "ext_resource") {
                if (!CheckAttributes(tag, attributes, {"type", "path", "id"}, {"type", "path", "id"}, why)) return fail(why);
                Resource resource;
                resource.type = *Attribute(attributes, "type");
                resource.path = *Attribute(attributes, "path");
                resource.id = *Attribute(attributes, "id");
                resource.line = lineNo;
                if (!OneOf(resource.type, kExternalTypes))
                    return fail("external resource type " + resource.type + " is not in the subset");
                if (!externalIds.insert(resource.id).second)
                    return fail("ext_resource id \"" + resource.id + "\" declared twice");
                out.external.push_back(std::move(resource));
                section = Section::External;
            } else if (tag == "sub_resource") {
                if (!CheckAttributes(tag, attributes, {"type", "id"}, {"type", "id"}, why)) return fail(why);
                Resource resource;
                resource.type = *Attribute(attributes, "type");
                resource.id = *Attribute(attributes, "id");
                resource.line = lineNo;
                if (!OneOf(resource.type, kEmbeddedTypes))
                    return fail("embedded resource type " + resource.type + " is not in the subset");
                if (!embeddedIds.insert(resource.id).second)
                    return fail("sub_resource id \"" + resource.id + "\" declared twice");
                out.embedded.push_back(std::move(resource));
                target = &out.embedded.back().properties;
                section = Section::Embedded;
            } else if (tag == "node") {
                if (!CheckAttributes(tag, attributes, {"name", "type", "parent"}, {"name", "type"}, why)) return fail(why);
                Node node;
                node.name = *Attribute(attributes, "name");
                node.type = *Attribute(attributes, "type");
                node.line = lineNo;
                if (!OneOf(node.type, kNodeTypes)) return fail("node type " + node.type + " is not in the subset");
                if (node.name.empty() || node.name.find('/') != std::string::npos)
                    return fail("node name '" + node.name + "' is not a plain name");

                const std::string* parent = Attribute(attributes, "parent");
                if (out.nodes.empty()) {
                    if (parent != nullptr) return fail("the first node is the root and takes no parent");
                    node.path = ".";
                } else {
                    if (parent == nullptr) return fail("a second root node, '" + node.name + "'");
                    if (nodeByPath.count(*parent) == 0)
                        return fail("parent \"" + *parent + "\" of '" + node.name + "' is not declared above it");
                    node.parent = *parent;
                    node.path = *parent == "." ? node.name : *parent + "/" + node.name;
                }
                if (!nodeByPath.emplace(node.path, out.nodes.size()).second)
                    return fail("node " + node.path + " declared twice");
                out.nodes.push_back(std::move(node));
                target = &out.nodes.back().properties;
                section = Section::Node;
            } else {
                return fail("section [" + tag + "] is not in the subset");
            }
            continue;
        }

        if (section == Section::None) return fail("the file must open with [gd_scene]");
        const std::size_t eq = line.find(" = ");
        if (eq == std::string_view::npos) return fail("expected 'key = value' or a [section]");
        const std::string key(Trim(line.substr(0, eq)));
        if (target == nullptr) return fail("property '" + key + "' in a section that takes none");

        const bool known = section == Section::Node
            ? (key.size() > kMetaPrefix.size() && key.starts_with(kMetaPrefix)) || OneOf(key, kNodeKeys)
            : OneOf(key, kEmbeddedKeys);
        if (!known) return fail("property '" + key + "' is not in the subset");
        if (!keysHere.insert(key).second) return fail("property '" + key + "' set twice");

        Value value;
        if (!ParseValue(line.substr(eq + 3), value, why)) return fail(key + ": " + why);
        if (value.kind == Value::Kind::ExtResource && externalIds.count(value.text) == 0)
            return fail(key + ": no ext_resource with id \"" + value.text + "\" above it");
        if (value.kind == Value::Kind::SubResource && embeddedIds.count(value.text) == 0)
            return fail(key + ": no sub_resource with id \"" + value.text + "\" above it");
        target->push_back({key, std::move(value)});
    }

    if (section == Section::None) {
        error = "end of file: no [gd_scene] - not a scene";
        return false;
    }
    if (out.nodes.empty()) {
        error = "end of file: the scene has no nodes";
        return false;
    }
    return true;
}

bool Load(const std::string& path, Scene& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot open";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (!Parse(buffer.str(), out, error)) {
        error = path + ": " + error;
        return false;
    }
    return true;
}

} // namespace MagicPortals::Tscn
