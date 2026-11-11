#pragma once

// One declaration per component, for a game's own components.
//
// Saving a component needs a writer and a reader registered with ComponentCodec.
// Editing it in the inspector needs a third piece of code, hand-written per
// component - the inspector is a very large file of exactly that. A game with a
// dozen components of its own would write four pieces for each, and the pieces
// drift: a field added to the struct and to the writer and forgotten in the
// reader reads back as its default, with nothing to say so.
//
// This is the cheap shape for the common case: a component that is a plain list
// of fields. Name them once, and the codec and the inspector both come from that
// list.
//
//     struct Health { int current{100}; int maximum{100}; float regen{0.5f};
//                     std::string label; glm::vec3 spawn{0.0f}; bool alive{true}; };
//
//     RegisterDescribedComponent<Health>("Health",
//         Field("current", &Health::current),
//         Field("maximum", &Health::maximum),
//         Field("regen",   &Health::regen),
//         Field("label",   &Health::label),
//         Field("spawn",   &Health::spawn),
//         Field("alive",   &Health::alive));
//
// WHAT IT IS NOT. Not reflection, and not a replacement for the engine's own
// components, which keep their hand-written codec and inspector code: they have
// units, ranges, asset pickers and per-field rules a name and a pointer cannot
// say. A component that needs those writes its own pair, as before; ComponentCodec
// is still open to it.
//
// Supported field types: bool, int, float, std::string, glm::vec2, glm::vec3,
// glm::vec4. Anything else is a compile error that names the type - a component
// with a field this cannot describe should be written by hand, not half-saved.
//
// THE FILE FORMAT is a game component's own, inside the scene's "Game" member where
// ComponentCodec puts every registered component, so it cannot collide with an
// engine key: { "current": 100, "label": "Boss", "spawn": [0, 1, 0] }. A key absent
// from a file leaves the field at the value a default-constructed component gives
// it, which is what makes adding a field later safe for scenes already saved.

#include "core/ComponentCodec.hpp"
#include "core/Json.hpp"
#include "core/JsonInts.hpp"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <ostream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace Supersonic {

// What the inspector (or any other tool) does with each field of a described
// component. Implemented in the editor with ImGui widgets and in tests by a
// recorder, so what a described component offers can be checked without a window.
// Each call gets the field's name and a reference to the live value; changing it
// edits the component.
class FieldVisitor {
public:
    virtual ~FieldVisitor() = default;
    virtual void Bool(const char* name, bool& value) = 0;
    virtual void Int(const char* name, int& value) = 0;
    virtual void Float(const char* name, float& value) = 0;
    virtual void String(const char* name, std::string& value) = 0;
    virtual void Vec2(const char* name, glm::vec2& value) = 0;
    virtual void Vec3(const char* name, glm::vec3& value) = 0;
    virtual void Vec4(const char* name, glm::vec4& value) = 0;
};

namespace ComponentFields {

// A component the inspector and the codec know about, with its operations erased
// to the registry and an entity.
struct Described {
    std::string key;
    std::function<bool(const entt::registry&, entt::entity)> has;
    std::function<void(entt::registry&, entt::entity)> add;
    std::function<void(entt::registry&, entt::entity)> remove;
    std::function<void(entt::registry&, entt::entity, FieldVisitor&)> visit;
};

// Every described component, in registration order.
const std::vector<Described>& All();

// Adds one. False (and nothing added) when the key is taken.
bool Add(Described described);

// Forgets them all. For tests, and for a game tearing down. Does not touch what
// ComponentCodec holds: ClearRegisteredComponents is its own call.
void Clear();

// --- one field ---------------------------------------------------------------

template <typename C, typename T>
struct FieldOf {
    const char* name;
    T C::*member;
};

template <typename C, typename T>
FieldOf<C, T> Field(const char* name, T C::*member) {
    return FieldOf<C, T>{name, member};
}

namespace detail {

template <typename T>
inline constexpr bool kAlwaysFalse = false;

// The four things done with a field, by type: write its JSON value, read it back,
// and hand it to a visitor. Anything not listed is a static_assert with its type in
// the message, rather than a field that silently does not save.

template <typename T>
void WriteValue(std::ostream& out, const T& value) {
    if constexpr (std::is_same_v<T, bool>) {
        out << (value ? "true" : "false");
    } else if constexpr (std::is_same_v<T, int>) {
        out << value;
    } else if constexpr (std::is_same_v<T, float>) {
        out << value;
    } else if constexpr (std::is_same_v<T, std::string>) {
        out << '"' << Json::Escape(value) << '"';
    } else if constexpr (std::is_same_v<T, glm::vec2>) {
        out << '[' << value.x << ", " << value.y << ']';
    } else if constexpr (std::is_same_v<T, glm::vec3>) {
        out << '[' << value.x << ", " << value.y << ", " << value.z << ']';
    } else if constexpr (std::is_same_v<T, glm::vec4>) {
        out << '[' << value.x << ", " << value.y << ", " << value.z << ", " << value.w << ']';
    } else {
        static_assert(kAlwaysFalse<T>,
                      "ComponentFields: this field type cannot be described. Supported: bool, int, "
                      "float, std::string, glm::vec2, glm::vec3, glm::vec4. Write a codec and an "
                      "inspector section by hand for a component that needs more.");
    }
}

// An array of N numbers into `out`, each falling back to what is already there: a
// file that holds a short or non-numeric array changes only the entries it has.
template <typename Vec>
void ReadVector(const Json::Value& node, Vec& out, int count) {
    if (!node.IsArray()) return;
    const auto& items = node.AsArray();
    for (int i = 0; i < count && static_cast<size_t>(i) < items.size(); ++i) {
        out[i] = items[static_cast<size_t>(i)].AsFloat(out[i]);
    }
}

template <typename T>
void ReadValue(const Json::Value& node, T& value) {
    if (node.GetType() == Json::Type::Null) return;   // absent: the field keeps what a default component gave it
    if constexpr (std::is_same_v<T, bool>) {
        value = node.AsBool(value);
    } else if constexpr (std::is_same_v<T, int>) {
        value = asI32(node.AsNumber(static_cast<double>(value)));
    } else if constexpr (std::is_same_v<T, float>) {
        value = node.AsFloat(value);
    } else if constexpr (std::is_same_v<T, std::string>) {
        value = node.AsString(value);
    } else if constexpr (std::is_same_v<T, glm::vec2>) {
        ReadVector(node, value, 2);
    } else if constexpr (std::is_same_v<T, glm::vec3>) {
        ReadVector(node, value, 3);
    } else if constexpr (std::is_same_v<T, glm::vec4>) {
        ReadVector(node, value, 4);
    } else {
        static_assert(kAlwaysFalse<T>, "ComponentFields: unsupported field type");
    }
}

template <typename T>
void VisitValue(FieldVisitor& visitor, const char* name, T& value) {
    if constexpr (std::is_same_v<T, bool>) {
        visitor.Bool(name, value);
    } else if constexpr (std::is_same_v<T, int>) {
        visitor.Int(name, value);
    } else if constexpr (std::is_same_v<T, float>) {
        visitor.Float(name, value);
    } else if constexpr (std::is_same_v<T, std::string>) {
        visitor.String(name, value);
    } else if constexpr (std::is_same_v<T, glm::vec2>) {
        visitor.Vec2(name, value);
    } else if constexpr (std::is_same_v<T, glm::vec3>) {
        visitor.Vec3(name, value);
    } else if constexpr (std::is_same_v<T, glm::vec4>) {
        visitor.Vec4(name, value);
    } else {
        static_assert(kAlwaysFalse<T>, "ComponentFields: unsupported field type");
    }
}

}  // namespace detail

}  // namespace ComponentFields

// Registers `C` with the codec (so scenes, prefabs, snapshots and undo carry it) and
// with the inspector (so it can be added, edited and removed), from one list of
// fields. Returns false, registering nothing, when the key is already taken by
// either; nothing is half-registered.
//
// Global, like ComponentCodec's own registration: it maps a TYPE to its format, and
// a type does not change meaning between scenes. Call it once at start-up, before
// the first scene is loaded.
template <typename C, typename... Ts>
bool RegisterDescribedComponent(std::string key, ComponentFields::FieldOf<C, Ts>... fields) {
    // The inspector's registry is checked first and the codec's second, and the
    // inspector entry is the one added last, so a clash leaves neither changed.
    for (const auto& existing : ComponentFields::All()) {
        if (existing.key == key) return false;
    }

    const auto members = std::make_tuple(fields...);

    const bool saved = ComponentCodec::RegisterComponent(
        key,
        // Writes only the VALUE: the codec owns the key and the punctuation around it.
        [members](const entt::registry& registry, entt::entity entity, std::ostream& out) {
            const C* component = registry.try_get<C>(entity);
            if (component == nullptr) return false;
            out << "{ ";
            bool first = true;
            std::apply(
                [&](const auto&... field) {
                    ((out << (first ? "" : ", ") << '"' << field.name << "\": ",
                      ComponentFields::detail::WriteValue(out, component->*(field.member)),
                      first = false),
                     ...);
                },
                members);
            out << " }";
            return true;
        },
        [members](entt::registry& registry, entt::entity entity, const Json::Value& value) {
            // A default-constructed component, replaced if one is already there, so
            // a field the file does not mention is what the type says it defaults to.
            C& component = registry.emplace_or_replace<C>(entity);
            std::apply(
                [&](const auto&... field) {
                    (ComponentFields::detail::ReadValue(value[field.name], component.*(field.member)), ...);
                },
                members);
        });
    if (!saved) return false;

    ComponentFields::Described described;
    described.key = key;
    described.has = [](const entt::registry& registry, entt::entity entity) {
        return registry.all_of<C>(entity);
    };
    described.add = [](entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<C>(entity)) registry.emplace<C>(entity);
    };
    described.remove = [](entt::registry& registry, entt::entity entity) {
        registry.remove<C>(entity);
    };
    described.visit = [members](entt::registry& registry, entt::entity entity, FieldVisitor& visitor) {
        C* component = registry.try_get<C>(entity);
        if (component == nullptr) return;
        std::apply(
            [&](const auto&... field) {
                (ComponentFields::detail::VisitValue(visitor, field.name, component->*(field.member)), ...);
            },
            members);
    };
    return ComponentFields::Add(std::move(described));
}

}  // namespace Supersonic
