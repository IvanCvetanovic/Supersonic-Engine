#pragma once

#include <cstddef>
#include <functional>
#include <ostream>
#include <string>

#include <entt/entt.hpp>

#include "core/Json.hpp"

namespace Supersonic {

// The single reader and writer for an entity's components.
//
// Scenes and prefabs each had their own, in different formats, covering
// different subsets: a prefab carried five components while a scene carried
// fifteen, so saving an entity as a prefab silently dropped its scripts, its
// lights, its audio, its animator and every collider but the box.
//
// Two writers over the same data will always drift - the only question is which
// component gets forgotten next, and how long before anyone notices. There is
// one now, and adding a component means editing one place.
namespace ComponentCodec {

// Writes every component the entity has, as JSON object members at the given
// indentation. Does NOT write the enclosing braces, and does not write the
// parent link: that is an index into a scene's entity array and means nothing
// to a standalone prefab.
//
// The final member is emitted without a trailing comma, so the caller can close
// the object immediately.
void Write(entt::registry& registry, entt::entity entity, std::ostream& out,
           const std::string& indent);

// Applies whatever the node describes onto an existing entity. Components the
// node does not mention are left alone, so this can be layered over defaults.
//
// Layering is the documented contract, and every branch used plain emplace,
// which asserts in a debug build when the component is already there and is
// undefined behaviour in a release one. It survived because the two callers
// that matter - loading a scene and instantiating a prefab - both start from a
// fresh entity. Applying a prefab over an existing entity, which is what the
// contract above promises, hit it immediately.
void Read(entt::registry& registry, entt::entity entity, const Json::Value& node);

// ---- A game's own components -------------------------------------------------
//
// The codec names eighteen engine components and could not be opened to
// anything else. That is a hard limit on what can be built on this engine: a
// mid-match save and a snapshot are made of a game's own component types, and
// neither could be written by the thing that writes every other component.
// A game's only options were to fork the codec or to keep a second serializer
// beside it - and two writers over the same data always drift, which is the
// argument this file was created to make.
//
// A registered component writes only its VALUE. The codec owns the key, the
// indentation and the punctuation, so a game cannot produce a file that fails
// to parse by forgetting a comma.
//
// Returns false when the entity does not have the component, in which case
// nothing is written for it.
using ComponentWriter =
    std::function<bool(const entt::registry& registry, entt::entity entity, std::ostream& out)>;

// Applies one previously written value back onto an entity.
using ComponentReader =
    std::function<void(entt::registry& registry, entt::entity entity, const Json::Value& value)>;

// Registers a component type under `key`.
//
// Everything registered is written inside a single "Game" member rather than
// beside the engine's own keys. That is not tidiness: it makes a collision
// between a game's component name and an engine one impossible, now and for
// every component the engine ever adds. A game that names something "Transform"
// is unremarkable, and it must not be a scene-corrupting mistake.
//
// Returns false if the key is already registered. Registration is global rather
// than per-registry, because it maps a TYPE to its format and a type does not
// change meaning between two scenes.
bool RegisterComponent(std::string key, ComponentWriter writer, ComponentReader reader);

// Forgets every registered component. For tests, and for a game tearing down.
void ClearRegisteredComponents();

std::size_t RegisteredComponentCount();

} // namespace ComponentCodec

} // namespace Supersonic
